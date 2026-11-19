import os
import sys
import unittest
from pathlib import Path

# Add bindings/python to sys.path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import ceebase


class TestCeebasePythonClient(unittest.TestCase):
    def setUp(self):
        self.db_path = "test_py_client.cbdb"
        self._cleanup()

    def tearDown(self):
        self._cleanup()

    def _cleanup(self):
        for ext in [".cbdb", ".wal"]:
            p = Path(self.db_path).with_suffix(ext)
            if p.exists():
                try:
                    p.unlink()
                except OSError:
                    pass

    def test_version_and_dbapi_attributes(self):
        self.assertEqual(ceebase.__version__, "1.0.0")
        self.assertEqual(ceebase.apilevel, "2.0")
        self.assertEqual(ceebase.paramstyle, "qmark")

    def test_typed_crud_lifecycle(self):
        with ceebase.connect(self.db_path) as conn:
            conn.execute("CREATE TABLE users (id INTEGER PRIMARY KEY, username TEXT, score DOUBLE);")
            
            # Parameterized insert with typed values
            cur = conn.cursor()
            cur.execute("INSERT INTO users VALUES (?, ?, ?);", (1, "Alice", 95.5))
            self.assertEqual(cur.rowcount, 1)

            cur.execute("INSERT INTO users VALUES (?, ?, ?);", (2, "Bob", 88.0))
            cur.execute("INSERT INTO users VALUES (?, ?, ?);", (3, "Charlie", 72.5))

            # Typed fetch
            cur.execute("SELECT id, username, score FROM users ORDER BY id;")
            rows = cur.fetchall()
            self.assertEqual(len(rows), 3)

            # Assert true types (not converted to strings!)
            self.assertEqual(rows[0][0], 1)
            self.assertIsInstance(rows[0][0], int)
            self.assertEqual(rows[0][1], "Alice")
            self.assertIsInstance(rows[0][1], str)
            self.assertEqual(rows[0][2], 95.5)
            self.assertIsInstance(rows[0][2], float)

            # Parameterized point lookup
            cur.execute("SELECT id, username, score FROM users WHERE id = ?;", (2,))
            user2 = cur.fetchone()
            self.assertEqual(user2, (2, "Bob", 88.0))

    def test_transaction_rollback(self):
        with ceebase.connect(self.db_path) as conn:
            conn.execute("CREATE TABLE ledger (id INTEGER PRIMARY KEY, amount DOUBLE);")
            conn.execute("INSERT INTO ledger VALUES (1, 100.0);")

        # Rollback inside transaction block
        conn = ceebase.connect(self.db_path)
        cur = conn.cursor()
        conn.execute("BEGIN;")
        conn.execute("INSERT INTO ledger VALUES (2, 200.0);")
        conn.rollback()

        cur.execute("SELECT COUNT(*) FROM ledger;")
        row = cur.fetchone()
        self.assertEqual(row[0], 1) # Only row 1 exists
        conn.close()

    def test_syntax_error_exception(self):
        with ceebase.connect(self.db_path) as conn:
            with self.assertRaises(ceebase.Error):
                conn.execute("SELECT * FROM non_existent_table;")


if __name__ == "__main__":
    unittest.main()
