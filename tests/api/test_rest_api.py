import json
import threading
import time
import unittest
import urllib.request
from pathlib import Path

# Add tools/studio to sys.path
import sys
REPO_ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(REPO_ROOT / "tools" / "studio"))
sys.path.insert(0, str(REPO_ROOT / "bindings" / "python"))

import server


class TestRestManagementApi(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.port = 8899
        cls.db_path = "test_rest_api.cbdb"
        for p in [Path(cls.db_path), Path("test_rest_api.wal")]:
            if p.exists(): p.unlink()

        cls.backend = server.StudioBackend(cls.db_path)
        server.StudioHandler.backend = cls.backend

        import socketserver
        class ThreadedServer(socketserver.ThreadingMixIn, server.http.server.HTTPServer):
            daemon_threads = True

        cls.httpd = ThreadedServer(("127.0.0.1", cls.port), server.StudioHandler)
        cls.thread = threading.Thread(target=cls.httpd.serve_forever, daemon=True)
        cls.thread.start()
        time.sleep(0.1)

    @classmethod
    def tearDownClass(cls):
        cls.httpd.shutdown()
        cls.httpd.server_close()
        for p in [Path(cls.db_path), Path("test_rest_api.wal")]:
            if p.exists():
                try: p.unlink()
                except OSError: pass

    def _get(self, path):
        req = urllib.request.Request(f"http://127.0.0.1:{self.port}{path}")
        with urllib.request.urlopen(req) as resp:
            return resp.status, json.loads(resp.read().decode("utf-8"))

    def _post(self, path, payload):
        data = json.dumps(payload).encode("utf-8")
        req = urllib.request.Request(f"http://127.0.0.1:{self.port}{path}", data=data, headers={"Content-Type": "application/json"})
        try:
            with urllib.request.urlopen(req) as resp:
                return resp.status, json.loads(resp.read().decode("utf-8"))
        except urllib.error.HTTPError as e:
            return e.code, json.loads(e.read().decode("utf-8"))

    def test_health_and_metrics(self):
        status, health = self._get("/api/v1/health")
        self.assertEqual(status, 200)
        self.assertEqual(health["status"], "healthy")
        self.assertEqual(health["storage"], "online")

        status, metrics = self._get("/api/v1/metrics")
        self.assertEqual(status, 200)
        self.assertIn("query_count", metrics)
        self.assertIn("buffer_pool_hit_ratio", metrics)

    def test_auth_login_and_invalid_credentials(self):
        # Valid login
        status, auth = self._post("/api/v1/auth/login", {
            "email": "admin@ceebase.local",
            "password": "CeeBaseAdmin2026!Secure"
        })
        self.assertEqual(status, 200)
        self.assertTrue(auth["success"])
        self.assertIn("token", auth)

        # Invalid login
        status, bad_auth = self._post("/api/v1/auth/login", {
            "email": "admin@ceebase.local",
            "password": "WrongPassword"
        })
        self.assertEqual(status, 401)
        self.assertFalse(bad_auth["success"])

    def test_query_execution_and_parameter_binding(self):
        # Execute query via REST API
        status, res = self._post("/api/v1/query", {
            "sql": "SELECT id, name, balance FROM users WHERE id = ?;",
            "parameters": [1]
        })
        self.assertEqual(status, 200)
        self.assertTrue(res["success"])
        self.assertEqual(len(res["rows"]), 1)
        self.assertEqual(res["rows"][0][1], "Alice Walker")

    def test_database_metadata_and_backups(self):
        status, dbs = self._get("/api/v1/databases")
        self.assertEqual(status, 200)
        self.assertEqual(len(dbs), 1)

        # Trigger backup snapshot
        status, backup_res = self._post("/api/v1/backups", {"name": "test_snap1.cbdb"})
        self.assertEqual(status, 200)
        self.assertTrue(backup_res["success"])

        # List backups
        status, backups = self._get("/api/v1/backups")
        self.assertEqual(status, 200)
        self.assertGreaterEqual(len(backups), 1)

        # Clean backup file
        p = Path("backups") / "test_snap1.cbdb"
        if p.exists(): p.unlink()


if __name__ == "__main__":
    unittest.main()
