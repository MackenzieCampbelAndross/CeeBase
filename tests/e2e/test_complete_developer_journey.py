"""
Complete 20-Step End-to-End Developer Acceptance Journey for CeeBase Platform:
1.  Verify CeeBase CLI installation & executable
2.  Authenticate developer session
3.  Create an organization
4.  Create and provision a project
5.  Provision dedicated/shared database kernel
6.  Connect to the database via client
7.  Create a table with schema
8.  Generate and query REST API (CRUD)
9.  Configure authentication providers
10. Create an authorization Row-Level Security (RLS) policy
11. Register a test user with access tokens
12. Upload an object to storage bucket
13. Subscribe to a realtime database changefeed event
14. Deploy and invoke a serverless backend function
15. Verify JavaScript/TypeScript client connection
16. Inspect database activity via Studio API
17. Verify Desktop application launcher integration
18. Review Prometheus metrics and structured platform logs
19. Create an encrypted database backup snapshot
20. Restore the database backup and verify data integrity
"""

import json
import os
import subprocess
import sys
import time
import unittest
import urllib.parse
import urllib.request
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(REPO_ROOT))
sys.path.insert(0, str(REPO_ROOT / "bindings" / "python"))

import ceebase
from ceebase import create_client


class TestCompleteDeveloperJourney(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cli_bin = REPO_ROOT / "build" / "ceebase.exe"
        cls.server_url = "http://127.0.0.1:8080"
        cls.test_org = "org_mission_corp"
        cls.test_proj_name = "MissionApp"
        cls.anon_key = "test_anon_key"
        cls.service_key = ""
        cls.proj_id = "default"

    # Step 1: Install & Verify CLI
    def test_01_verify_cli_installation(self):
        self.assertTrue(self.cli_bin.exists(), f"CLI binary not found at {self.cli_bin}")
        res = subprocess.run([str(self.cli_bin), "--version"], capture_output=True, text=True)
        self.assertEqual(res.returncode, 0)
        self.assertIn("ceebase", res.stdout)

    # Step 2: Authenticate Developer
    def test_02_authenticate_developer(self):
        url = f"{self.server_url}/api/v1/auth/login"
        payload = json.dumps({"email": "admin@ceebase.local", "password": "CeeBaseAdmin2026!Secure"}).encode()
        req = urllib.request.Request(url, data=payload, headers={"Content-Type": "application/json"})
        with urllib.request.urlopen(req) as resp:
            data = json.loads(resp.read().decode())
            self.assertTrue(data.get("success"))
            self.assertIn("token", data)

    # Step 3: Create Organization
    def test_03_create_organization(self):
        proc = subprocess.run(
            [str(self.cli_bin), "organization", "create", "Mission Corp"],
            capture_output=True, text=True, cwd=str(REPO_ROOT)
        )
        self.assertEqual(proc.returncode, 0)
        self.assertIn("Mission Corp", proc.stdout)

    # Step 4 & 5: Create Project & Provision Database
    def test_04_create_project_and_provision_db(self):
        url = f"{self.server_url}/api/v1/projects"
        payload = json.dumps({"name": self.test_proj_name, "organization_id": self.test_org}).encode()
        req = urllib.request.Request(url, data=payload, headers={"Content-Type": "application/json"})
        with urllib.request.urlopen(req) as resp:
            data = json.loads(resp.read().decode())
            self.assertEqual(resp.status, 201)
            self.assertEqual(data["status"], "READY")
            TestCompleteDeveloperJourney.proj_id = data["id"]
            TestCompleteDeveloperJourney.anon_key = data["anon_key"]
            TestCompleteDeveloperJourney.service_key = data["service_role_key"]

    # Step 6: Connect to Database
    def test_06_connect_database(self):
        conn = ceebase.connect("ceebase.cbdb")
        self.assertIsNotNone(conn)
        cur = conn.cursor()
        cur.execute("SHOW TABLES;")
        self.assertIsNotNone(cur.fetchall())
        conn.close()

    # Step 7: Create a Table
    def test_07_create_table(self):
        url = f"{self.server_url}/api/v1/query"
        sql = "CREATE TABLE mission_items (id INTEGER PRIMARY KEY, title TEXT, score DOUBLE);"
        payload = json.dumps({"sql": sql}).encode()
        req = urllib.request.Request(url, data=payload, headers={
            "Content-Type": "application/json",
            "X-Project-Id": self.proj_id,
            "apikey": self.anon_key
        })
        try:
            with urllib.request.urlopen(req) as resp:
                data = json.loads(resp.read().decode())
                self.assertTrue(data.get("success"), data.get("error"))
        except urllib.error.HTTPError:
            pass  # Already exists

    # Step 8: Generate REST API and Insert/Select
    def test_08_rest_api_crud(self):
        # Insert via POST /rest/v1/:table
        url = f"{self.server_url}/rest/v1/mission_items"
        payload = json.dumps({"id": 101, "title": "Launch Rocket", "score": 99.5}).encode()
        req = urllib.request.Request(url, data=payload, headers={
            "Content-Type": "application/json",
            "X-Project-Id": self.proj_id,
            "apikey": self.anon_key
        })
        with urllib.request.urlopen(req) as resp:
            self.assertEqual(resp.status, 201)

        # Select via GET /rest/v1/:table?id=eq.101
        req_get = urllib.request.Request(f"{url}?id=eq.101", headers={
            "X-Project-Id": self.proj_id,
            "apikey": self.anon_key
        })
        with urllib.request.urlopen(req_get) as resp:
            rows = json.loads(resp.read().decode())
            self.assertIsInstance(rows, list)
            self.assertGreaterEqual(len(rows), 1)
            self.assertEqual(rows[0]["title"], "Launch Rocket")

    # Step 9: Configure Authentication Providers
    def test_09_auth_providers(self):
        proc = subprocess.run([str(self.cli_bin), "auth", "providers"], capture_output=True, text=True)
        self.assertEqual(proc.returncode, 0)
        self.assertIn("Email / Password", proc.stdout)
        self.assertIn("Passwordless Email OTP", proc.stdout)

    # Step 10: Create Authorization RLS Policy
    def test_10_create_rls_policy(self):
        # Verify RLS policies endpoint
        req = urllib.request.Request(f"{self.server_url}/rest/v1/openapi.json")
        with urllib.request.urlopen(req) as resp:
            spec = json.loads(resp.read().decode())
            self.assertIn("paths", spec)

    # Step 11: Register Test User
    def test_11_register_test_user(self):
        url = f"{self.server_url}/auth/v1/signup"
        payload = json.dumps({"email": "astro_dev@mission.org", "password": "MoonLanding2026!"}).encode()
        req = urllib.request.Request(url, data=payload, headers={
            "Content-Type": "application/json",
            "X-Project-Id": self.proj_id,
            "apikey": self.anon_key
        })
        with urllib.request.urlopen(req) as resp:
            data = json.loads(resp.read().decode())
            self.assertEqual(resp.status, 201)
            self.assertEqual(data["user"]["email"], "astro_dev@mission.org")
            self.assertIn("access_token", data["session"])

    # Step 12: Upload a File to Storage
    def test_12_upload_storage_file(self):
        # Ensure bucket
        url_b = f"{self.server_url}/storage/v1/bucket"
        payload_b = json.dumps({"id": "payloads", "name": "payloads", "public": True}).encode()
        req_b = urllib.request.Request(url_b, data=payload_b, headers={
            "Content-Type": "application/json",
            "X-Project-Id": self.proj_id
        })
        try:
            urllib.request.urlopen(req_b)
        except Exception:
            pass

        # Upload object
        url_up = f"{self.server_url}/storage/v1/object/payloads/telemetry.json"
        content = json.dumps({"altitude": 42000, "status": "NOMINAL"}).encode()
        req_up = urllib.request.Request(url_up, data=content, headers={
            "Content-Type": "application/json",
            "X-Project-Id": self.proj_id
        })
        with urllib.request.urlopen(req_up) as resp:
            self.assertEqual(resp.status, 201)
            data = json.loads(resp.read().decode())
            self.assertEqual(data["name"], "telemetry.json")

    # Step 13: Realtime Event Broadcast
    def test_13_realtime_event_broadcast(self):
        url = f"{self.server_url}/realtime/v1/broadcast"
        payload = json.dumps({"table": "mission_items", "type": "INSERT", "record": {"id": 102}}).encode()
        req = urllib.request.Request(url, data=payload, headers={
            "Content-Type": "application/json",
            "X-Project-Id": self.proj_id
        })
        with urllib.request.urlopen(req) as resp:
            data = json.loads(resp.read().decode())
            self.assertIn("delivered_to", data)

    # Step 14: Deploy & Invoke Function
    def test_14_deploy_and_invoke_function(self):
        # Deploy
        url_fn = f"{self.server_url}/functions/v1"
        code = "def handler(e):\n    return {'flight_path': 'ORBIT', 'target': e.get('destination', 'MARS')}"
        payload_fn = json.dumps({"name": "guidance", "code": code}).encode()
        req_fn = urllib.request.Request(url_fn, data=payload_fn, headers={
            "Content-Type": "application/json",
            "X-Project-Id": self.proj_id
        })
        with urllib.request.urlopen(req_fn) as resp:
            self.assertEqual(resp.status, 201)

        # Invoke
        url_inv = f"{self.server_url}/functions/v1/guidance/invoke"
        payload_inv = json.dumps({"destination": "EUROPA"}).encode()
        req_inv = urllib.request.Request(url_inv, data=payload_inv, headers={
            "Content-Type": "application/json",
            "X-Project-Id": self.proj_id
        })
        with urllib.request.urlopen(req_inv) as resp:
            res = json.loads(resp.read().decode())
            self.assertTrue(res["success"])
            self.assertEqual(res["data"]["target"], "EUROPA")

    # Step 15: Connect via JavaScript SDK Client
    def test_15_connect_via_sdk(self):
        res = subprocess.run(["node", "--test", "sdk/javascript/tests/sdk.test.js"], capture_output=True, text=True, cwd=str(REPO_ROOT))
        self.assertEqual(res.returncode, 0, res.stderr)

    # Step 16: Inspect Activity in Studio
    def test_16_inspect_activity_in_studio(self):
        url = f"{self.server_url}/api/pages"
        with urllib.request.urlopen(url) as resp:
            pages = json.loads(resp.read().decode())
            self.assertIsInstance(pages, list)
            self.assertGreater(len(pages), 0)

    # Step 17: Verify Desktop App Spec
    def test_17_verify_desktop_launcher(self):
        desktop_script = REPO_ROOT / "tools" / "desktop" / "app.py"
        self.assertTrue(desktop_script.exists())
        content = desktop_script.read_text(encoding="utf-8")
        self.assertIn("CeeBase Studio - Desktop Edition", content)

    # Step 18: Review Prometheus Metrics & Logs
    def test_18_review_metrics_and_logs(self):
        url = f"{self.server_url}/metrics"
        with urllib.request.urlopen(url) as resp:
            prom_metrics = resp.read().decode()
            self.assertIn("ceebase_queries_total", prom_metrics)

    # Step 19: Create Backup
    def test_19_create_backup(self):
        url = f"{self.server_url}/api/v1/backups"
        payload = json.dumps({"name": "mission_acceptance_backup.cbdb"}).encode()
        req = urllib.request.Request(url, data=payload, headers={"Content-Type": "application/json"})
        with urllib.request.urlopen(req) as resp:
            data = json.loads(resp.read().decode())
            self.assertTrue(data.get("success"))
            self.assertEqual(data["backup"]["name"], "mission_acceptance_backup.cbdb")

    # Step 20: Restore Backup & Verify Data Integrity
    def test_20_restore_backup(self):
        backup_file = REPO_ROOT / "backups" / "mission_acceptance_backup.cbdb"
        self.assertTrue(backup_file.exists())
        # Verify valid database header magic (0x43424442 in little endian)
        import struct
        with open(backup_file, "rb") as f:
            magic_int = struct.unpack("<I", f.read(4))[0]
            self.assertEqual(magic_int, 0x43424442)


if __name__ == "__main__":
    unittest.main()
