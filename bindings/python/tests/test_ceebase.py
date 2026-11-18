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

    def test_version(self):
        self.assertEqual(ceebase.__version__, "1.0.0")

    def test_crud_lifecycle(self):
        with ceebase.connect(self.db_path) as conn:
            conn.execute("CREATE TABLE users (id INTEGER PRIMARY KEY, username TEXT, score DOUBLE);")
            cur = conn.execute("INSERT INTO users VALUES (1, 'Alice', 95.5);")
            self.assertEqual(cur.rowcount, 1)

            conn.execute("INSERT INTO users VALUES (2, 'Bob', 88.0);")
            conn.execute("INSERT INTO users VALUES (3, 'Charlie', 72.5);")

            cur = conn.execute("SELECT id, username, score FROM users ORDER BY id;")
            rows = cur.fetchall()
            self.assertEqual(len(rows), 3)
            self.assertEqual(rows[0][0], "1")
            self.assertEqual(rows[0][1], "Alice")
            self.assertEqual(rows[1][1], "Bob")
            self.assertEqual(rows[2][1], "Charlie")

    def test_syntax_error_exception(self):
        with ceebase.connect(self.db_path) as conn:
            with self.assertRaises(ceebase.Error):
                conn.execute("SELECT * FROM non_existent_table;")


if __name__ == "__main__":
    unittest.main()
