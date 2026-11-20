"""
Automated Contract Tests for Python CeeBase Platform SDK
"""

import sys
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent.parent
sys.path.insert(0, str(REPO_ROOT))
sys.path.insert(0, str(REPO_ROOT / "bindings" / "python"))

from ceebase import create_client


class TestPythonSDK(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.client = create_client("http://127.0.0.1:8080", "test_anon_key")

    def test_client_init(self):
        self.assertEqual(self.client.url, "http://127.0.0.1:8080")
        self.assertEqual(self.client.key, "test_anon_key")
        self.assertIsNotNone(self.client.auth)
        self.assertIsNotNone(self.client.storage)
        self.assertIsNotNone(self.client.functions)

    def test_query_builder_dsl(self):
        qb = self.client.from_("users").select("id, name").eq("id", 1).order("id", ascending=False).limit(5)
        self.assertEqual(qb.table, "users")
        self.assertEqual(qb.params["select"], "id, name")
        self.assertEqual(qb.params["id"], "eq.1")
        self.assertEqual(qb.params["order"], "id.desc")
        self.assertEqual(qb.params["limit"], "5")

    def test_live_query_execution(self):
        data, err = self.client.from_("users").select("*").execute()
        self.assertIsNone(err, f"Query error: {err}")
        self.assertIsInstance(data, list)
        self.assertGreaterEqual(len(data), 1)

    def test_signed_url_generation(self):
        url, err = self.client.storage.from_("media").create_signed_url("profile.png", 60)
        self.assertIsNone(err, f"Storage error: {err}")
        self.assertIn("/storage/v1/object/", url)
        self.assertIn("token=", url)


if __name__ == "__main__":
    unittest.main()
