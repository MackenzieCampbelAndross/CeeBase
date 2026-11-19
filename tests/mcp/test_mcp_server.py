#!/usr/bin/env python3
"""
Comprehensive Automated Test Suite for CeeBase MCP Server
Validates protocol compliance, authentication, scopes, cross-tenant isolation,
read-only protections, parameter bindings, and audit logging.
"""

import json
import os
import sys
import unittest
import time
from pathlib import Path

# Add project root to sys.path
REPO_ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(REPO_ROOT))
sys.path.insert(0, str(REPO_ROOT / "bindings" / "python"))

from mcp.server import (
    CeebaseMCPServer,
    AgentSession,
    handle_request,
    get_tools_list,
    AUDIT_LOG_FILE
)


class TestCeebaseMCPServer(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.db_path = str(REPO_ROOT / "test_mcp_sandbox.cbdb")
        # Ensure fresh database
        if os.path.exists(cls.db_path):
            os.remove(cls.db_path)
        wal = cls.db_path + ".wal"
        if os.path.exists(wal):
            os.remove(wal)

        cls.server = CeebaseMCPServer(cls.db_path)
        # Seed test table
        cls.server.execute_sql("CREATE TABLE sensors (id INT PRIMARY KEY, sensor_type VARCHAR, reading DOUBLE, active BOOLEAN);")
        cls.server.execute_sql("INSERT INTO sensors VALUES (1, 'temperature', 23.5, true);")
        cls.server.execute_sql("INSERT INTO sensors VALUES (2, 'humidity', 58.2, true);")
        cls.server.execute_sql("INSERT INTO sensors VALUES (3, 'pressure', 1013.25, false);")

        # Setup test sessions
        cls.rw_token = "tok_readwrite_123"
        cls.ro_token = "tok_readonly_456"
        cls.expired_token = "tok_expired_789"
        cls.acme_token = "tok_tenant_acme"

        cls.server.register_session(AgentSession(
            token=cls.rw_token,
            agent_id="agent-rw",
            org_id="org-main",
            database=cls.db_path,
            scopes=["read-only", "read-write"]
        ))
        cls.server.register_session(AgentSession(
            token=cls.ro_token,
            agent_id="agent-ro",
            org_id="org-main",
            database=cls.db_path,
            scopes=["read-only"]
        ))
        cls.server.register_session(AgentSession(
            token=cls.expired_token,
            agent_id="agent-expired",
            org_id="org-main",
            database=cls.db_path,
            scopes=["read-only"],
            expires_at=time.time() - 3600  # expired 1 hour ago
        ))
        cls.server.register_session(AgentSession(
            token=cls.acme_token,
            agent_id="agent-acme",
            org_id="org-acme",
            database="acme_prod.cbdb",
            scopes=["read-only", "read-write"]
        ))

    @classmethod
    def tearDownClass(cls):
        if cls.server._conn:
            cls.server._conn.close()
        for p in [cls.db_path, cls.db_path + ".wal"]:
            if os.path.exists(p):
                try:
                    os.remove(p)
                except Exception:
                    pass

    def test_01_initialize_and_ping(self):
        req = json.dumps({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}})
        res = handle_request(self.server, req)
        self.assertEqual(res["id"], 1)
        self.assertIn("serverInfo", res["result"])
        self.assertEqual(res["result"]["serverInfo"]["name"], "ceebase-mcp-server")

        ping_req = json.dumps({"jsonrpc": "2.0", "id": 2, "method": "ping"})
        ping_res = handle_request(self.server, ping_req)
        self.assertEqual(ping_res["id"], 2)
        self.assertEqual(ping_res["result"], {})

    def test_02_tools_list_spec(self):
        req = json.dumps({"jsonrpc": "2.0", "id": 3, "method": "tools/list"})
        res = handle_request(self.server, req)
        tools = res["result"]["tools"]
        tool_names = [t["name"] for t in tools]
        expected_tools = [
            "list_databases",
            "list_tables",
            "describe_table",
            "execute_read_query",
            "execute_write_query",
            "explain_query",
            "inspect_database",
            "get_database_health"
        ]
        for et in expected_tools:
            self.assertIn(et, tool_names, f"Tool {et} must be declared in tools/list")

    def test_03_authentication_and_revocation(self):
        # Expired token must be rejected
        req = json.dumps({
            "jsonrpc": "2.0", "id": 4, "method": "tools/call",
            "params": {"name": "list_tables", "arguments": {}, "token": self.expired_token}
        })
        res = handle_request(self.server, req)
        self.assertIn("error", res)
        self.assertEqual(res["error"]["code"], -32001)

        # Revoked token must be rejected
        revoked_token = "tok_to_be_revoked"
        self.server.register_session(AgentSession(
            token=revoked_token,
            agent_id="agent-revoked",
            org_id="org-main",
            database=self.db_path,
            scopes=["read-only"]
        ))
        self.server.revoke_token(revoked_token)

        req_rev = json.dumps({
            "jsonrpc": "2.0", "id": 5, "method": "tools/call",
            "params": {"name": "list_tables", "arguments": {}, "token": revoked_token}
        })
        res_rev = handle_request(self.server, req_rev)
        self.assertIn("error", res_rev)
        self.assertEqual(res_rev["error"]["code"], -32001)

    def test_04_read_only_query_and_params(self):
        # execute_read_query with valid SELECT and parameters
        req = json.dumps({
            "jsonrpc": "2.0", "id": 6, "method": "tools/call",
            "params": {
                "name": "execute_read_query",
                "arguments": {
                    "query": "SELECT * FROM sensors WHERE id = ?;",
                    "parameters": [2]
                },
                "token": self.ro_token
            }
        })
        res = handle_request(self.server, req)
        self.assertNotIn("error", res)
        content_text = json.loads(res["result"]["content"][0]["text"])
        self.assertTrue(content_text["success"])
        self.assertEqual(len(content_text["rows"]), 1)
        self.assertEqual(content_text["rows"][0][1], "humidity")

    def test_05_read_only_protection_against_destructive_sql(self):
        # execute_read_query must refuse INSERT / DROP / UPDATE statements
        for bad_stmt in [
            "INSERT INTO sensors VALUES (4, 'gas', 1.0, true);",
            "DROP TABLE sensors;",
            "DELETE FROM sensors WHERE id = 1;",
            "UPDATE sensors SET reading = 0.0;"
        ]:
            req = json.dumps({
                "jsonrpc": "2.0", "id": 7, "method": "tools/call",
                "params": {
                    "name": "execute_read_query",
                    "arguments": {"query": bad_stmt},
                    "token": self.ro_token
                }
            })
            res = handle_request(self.server, req)
            content_text = json.loads(res["result"]["content"][0]["text"])
            self.assertFalse(content_text["success"])
            self.assertIn("Security violation", content_text["error"])

    def test_06_execute_write_query_scope_enforcement(self):
        # Read-only token attempting execute_write_query must fail
        req_ro = json.dumps({
            "jsonrpc": "2.0", "id": 8, "method": "tools/call",
            "params": {
                "name": "execute_write_query",
                "arguments": {
                    "query": "INSERT INTO sensors VALUES (4, 'light', 350.0, true);"
                },
                "token": self.ro_token
            }
        })
        res_ro = handle_request(self.server, req_ro)
        content_ro = json.loads(res_ro["result"]["content"][0]["text"])
        self.assertFalse(content_ro["success"])
        self.assertIn("Permission Denied", content_ro["error"])

        # Read-write token must succeed
        req_rw = json.dumps({
            "jsonrpc": "2.0", "id": 9, "method": "tools/call",
            "params": {
                "name": "execute_write_query",
                "arguments": {
                    "query": "INSERT INTO sensors VALUES (4, 'light', 350.0, true);"
                },
                "token": self.rw_token
            }
        })
        res_rw = handle_request(self.server, req_rw)
        content_rw = json.loads(res_rw["result"]["content"][0]["text"])
        self.assertTrue(content_rw["success"])
        self.assertEqual(content_rw["rows_affected"], 1)

    def test_07_cross_tenant_isolation(self):
        # Agent with org-acme attempting to access org-main or another database must be denied
        req = json.dumps({
            "jsonrpc": "2.0", "id": 10, "method": "tools/call",
            "params": {
                "name": "execute_read_query",
                "arguments": {
                    "organization_id": "org-main",
                    "query": "SELECT * FROM sensors;"
                },
                "token": self.acme_token
            }
        })
        res = handle_request(self.server, req)
        self.assertIn("error", res)
        self.assertEqual(res["error"]["code"], -32003)
        self.assertIn("Cross-tenant access denied", res["error"]["message"])

    def test_08_identifier_validation(self):
        # Describe table with SQL injection attempt in table name
        req_bad = json.dumps({
            "jsonrpc": "2.0", "id": 11, "method": "tools/call",
            "params": {
                "name": "describe_table",
                "arguments": {"table_name": "sensors; DROP TABLE sensors;--"},
                "token": self.ro_token
            }
        })
        res_bad = handle_request(self.server, req_bad)
        content_bad = json.loads(res_bad["result"]["content"][0]["text"])
        self.assertIn("error", content_bad)
        self.assertIn("Invalid table name identifier", content_bad["error"])

        # Describe valid table
        req_good = json.dumps({
            "jsonrpc": "2.0", "id": 12, "method": "tools/call",
            "params": {
                "name": "describe_table",
                "arguments": {"table_name": "sensors"},
                "token": self.ro_token
            }
        })
        res_good = handle_request(self.server, req_good)
        content_good = json.loads(res_good["result"]["content"][0]["text"])
        self.assertEqual(content_good["table_name"], "sensors")
        self.assertEqual(content_good["total_rows"], 4)

    def test_09_database_health_and_inspection(self):
        # Inspect database
        req_insp = json.dumps({
            "jsonrpc": "2.0", "id": 13, "method": "tools/call",
            "params": {"name": "inspect_database", "arguments": {}, "token": self.ro_token}
        })
        res_insp = handle_request(self.server, req_insp)
        content_insp = json.loads(res_insp["result"]["content"][0]["text"])
        self.assertIn("total_pages_allocated", content_insp)
        self.assertEqual(content_insp["page_size_bytes"], 4096)

        # Database health
        req_health = json.dumps({
            "jsonrpc": "2.0", "id": 14, "method": "tools/call",
            "params": {"name": "get_database_health", "arguments": {}, "token": self.ro_token}
        })
        res_health = handle_request(self.server, req_health)
        content_health = json.loads(res_health["result"]["content"][0]["text"])
        self.assertEqual(content_health["status"], "HEALTHY")
        self.assertEqual(content_health["header_integrity"], "VALID_MAGIC_CBDB")

    def test_10_audit_logging_persisted(self):
        # Audit log must exist and have recorded actions
        self.assertTrue(AUDIT_LOG_FILE.exists())
        with open(AUDIT_LOG_FILE, "r", encoding="utf-8") as f:
            lines = f.readlines()
        self.assertGreater(len(lines), 0)
        last_entry = json.loads(lines[-1])
        self.assertIn("agent_id", last_entry)
        self.assertIn("tool", last_entry)
        self.assertIn("status", last_entry)
        self.assertIn("duration_ms", last_entry)


if __name__ == "__main__":
    unittest.main()
