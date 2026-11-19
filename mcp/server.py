#!/usr/bin/env python3
"""
Official CeeBase Model Context Protocol (MCP) Server
Allows AI agents (Claude, Antigravity, Cursor) to directly query and manage CeeBase databases.
Conforms to MCP standard JSON-RPC 2.0 stdio transport.
"""

import json
import os
import struct
import sys
import time
from pathlib import Path

# Add bindings/python to sys.path
REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT / "bindings" / "python"))

import ceebase

DEFAULT_DB_FILE = "ceebase.cbdb"


class CeebaseMCPServer:
    def __init__(self, db_path: str = DEFAULT_DB_FILE):
        self.db_path = db_path
        self._conn = None
        self._ensure_conn()

    def _ensure_conn(self):
        if self._conn is None:
            self._conn = ceebase.connect(self.db_path)

    def execute_sql(self, sql: str):
        self._ensure_conn()
        start = time.perf_counter()
        try:
            # Handle multiple statements separated by ';'
            statements = [s.strip() for s in sql.strip().split(';') if s.strip()]
            if not statements:
                return {
                    "success": True,
                    "columns": [],
                    "rows": [],
                    "rows_affected": 0,
                    "execution_time_ms": 0.0,
                    "error": None
                }

            last_cur = None
            total_affected = 0
            for stmt in statements:
                last_cur = self._conn.execute(stmt)
                total_affected += last_cur.rowcount

            cols = [c[0] for c in last_cur.description] if last_cur and last_cur.description else []
            rows = last_cur.fetchall() if last_cur else []
            elapsed_ms = (time.perf_counter() - start) * 1000.0
            return {
                "success": True,
                "columns": cols,
                "rows": rows,
                "rows_affected": total_affected,
                "execution_time_ms": round(elapsed_ms, 3),
                "error": None
            }
        except Exception as e:
            elapsed_ms = (time.perf_counter() - start) * 1000.0
            return {
                "success": False,
                "columns": [],
                "rows": [],
                "rows_affected": 0,
                "execution_time_ms": round(elapsed_ms, 3),
                "error": str(e)
            }

    def list_tables(self):
        res = self.execute_sql("SHOW TABLES;")
        if not res["success"]:
            return {"tables": [], "error": res["error"]}

        tables = []
        for r in res["rows"]:
            tbl_name = r[0]
            cnt_res = self.execute_sql(f"SELECT COUNT(*) FROM {tbl_name};")
            count = cnt_res["rows"][0][0] if cnt_res["success"] and cnt_res["rows"] else "0"
            tables.append({
                "table_name": tbl_name,
                "row_count": count
            })
        return {"tables": tables}

    def describe_table(self, table_name: str):
        res = self.execute_sql(f"SELECT * FROM {table_name} LIMIT 1;")
        if not res["success"]:
            return {"error": res["error"]}

        count_res = self.execute_sql(f"SELECT COUNT(*) FROM {table_name};")
        count = count_res["rows"][0][0] if count_res["success"] and count_res["rows"] else "0"

        return {
            "table_name": table_name,
            "columns": res["columns"],
            "total_rows": count
        }

    def explain_query(self, query: str):
        if not query.strip().upper().startswith("EXPLAIN"):
            query = f"EXPLAIN {query}"
        return self.execute_sql(query)

    def get_storage_diagnostics(self):
        p = Path(self.db_path)
        if not p.exists():
            return {"error": "Database file does not exist"}

        size = p.stat().st_size
        total_pages = size // 4096
        pages = []

        with open(p, "rb") as f:
            for pid in range(min(total_pages, 20)):
                raw = f.read(4096)
                if len(raw) < 4096:
                    break
                if pid == 0:
                    magic, v_maj, v_min, page_size, total_p, free_p, cat_p = struct.unpack("<IHHIIII", raw[0:24])
                    crc = struct.unpack("<I", raw[4092:4096])[0]
                    pages.append({
                        "page_id": pid,
                        "type": "HEADER_PAGE",
                        "magic": f"0x{magic:08X}",
                        "crc32": f"0x{crc:08X}",
                        "total_pages": total_p
                    })
                elif pid == 1:
                    pages.append({"page_id": pid, "type": "CATALOG_PAGE"})
                else:
                    page_id, lsn, free_ptr, slot_count = struct.unpack("<IQHH", raw[0:16])
                    pages.append({
                        "page_id": pid,
                        "type": "SLOTTED_DATA_PAGE",
                        "slot_count": slot_count,
                        "free_space_offset": free_ptr
                    })

        return {
            "database_file": str(p.name),
            "file_size_bytes": size,
            "total_pages_allocated": total_pages,
            "page_size": 4096,
            "pages_inspected": pages
        }


def make_jsonrpc_response(req_id, result=None, error=None):
    resp = {"jsonrpc": "2.0", "id": req_id}
    if error is not None:
        resp["error"] = error
    else:
        resp["result"] = result
    return resp


def get_tools_list():
    return [
        {
            "name": "execute_sql",
            "description": "Execute any SQL query against CeeBase (CREATE, INSERT, SELECT, UPDATE, DELETE, BEGIN, COMMIT).",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "query": {
                        "type": "string",
                        "description": "The exact SQL statement to execute, ending with ';'."
                    }
                },
                "required": ["query"]
            }
        },
        {
            "name": "list_tables",
            "description": "List all tables in the CeeBase database and their current row counts.",
            "inputSchema": {
                "type": "object",
                "properties": {}
            }
        },
        {
            "name": "describe_table",
            "description": "Get schema details and column names for a specific table in CeeBase.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "table_name": {
                        "type": "string",
                        "description": "The name of the table to inspect."
                    }
                },
                "required": ["table_name"]
            }
        },
        {
            "name": "explain_query",
            "description": "Get the logical execution plan, physical B+ Tree index access path, and cost estimation for a query.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "query": {
                        "type": "string",
                        "description": "The SQL query to explain."
                    }
                },
                "required": ["query"]
            }
        },
        {
            "name": "get_storage_diagnostics",
            "description": "Inspect low-level 4 KB fixed pages, CRC-32 integrity checksums, and storage layout in the .cbdb database file.",
            "inputSchema": {
                "type": "object",
                "properties": {}
            }
        }
    ]


def handle_request(server: CeebaseMCPServer, line: str):
    try:
        req = json.loads(line)
    except Exception as e:
        return make_jsonrpc_response(None, error={"code": -32700, "message": f"Parse error: {e}"})

    req_id = req.get("id")
    method = req.get("method")
    params = req.get("params", {})

    if method == "initialize":
        return make_jsonrpc_response(req_id, result={
            "protocolVersion": "2024-11-05",
            "capabilities": {
                "tools": {}
            },
            "serverInfo": {
                "name": "ceebase-mcp-server",
                "version": "1.0.0"
            }
        })
    elif method == "notifications/initialized":
        return None  # No response needed for notification
    elif method == "tools/list":
        return make_jsonrpc_response(req_id, result={"tools": get_tools_list()})
    elif method == "tools/call":
        tool_name = params.get("name")
        args = params.get("arguments", {})

        if tool_name == "execute_sql":
            query = args.get("query") or args.get("sql", "")
            res = server.execute_sql(query)
            return make_jsonrpc_response(req_id, result={"content": [{"type": "text", "text": json.dumps(res, indent=2)}]})
        elif tool_name == "list_tables":
            res = server.list_tables()
            return make_jsonrpc_response(req_id, result={"content": [{"type": "text", "text": json.dumps(res, indent=2)}]})
        elif tool_name == "describe_table":
            res = server.describe_table(args.get("table_name", ""))
            return make_jsonrpc_response(req_id, result={"content": [{"type": "text", "text": json.dumps(res, indent=2)}]})
        elif tool_name == "explain_query":
            res = server.explain_query(args.get("query", ""))
            return make_jsonrpc_response(req_id, result={"content": [{"type": "text", "text": json.dumps(res, indent=2)}]})
        elif tool_name == "get_storage_diagnostics":
            res = server.get_storage_diagnostics()
            return make_jsonrpc_response(req_id, result={"content": [{"type": "text", "text": json.dumps(res, indent=2)}]})
        else:
            return make_jsonrpc_response(req_id, error={"code": -32601, "message": f"Unknown tool: {tool_name}"})
    elif method == "ping":
        return make_jsonrpc_response(req_id, result={})
    else:
        return make_jsonrpc_response(req_id, error={"code": -32601, "message": f"Method not found: {method}"})


def main():
    db_file = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_DB_FILE
    server = CeebaseMCPServer(db_file)

    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        resp = handle_request(server, line)
        if resp is not None:
            sys.stdout.write(json.dumps(resp) + "\n")
            sys.stdout.flush()


if __name__ == "__main__":
    main()
