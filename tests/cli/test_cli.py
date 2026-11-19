#!/usr/bin/env python3
"""
Automated Test Suite for CeeBase Rust CLI
Validates commands: init, inspect, status, query, query --plain,
backup, restore, database, user, organization, config, and error handling.
"""

import os
import subprocess
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent

# Find CLI binary (prefer release, fallback to debug)
CLI_RELEASE = REPO_ROOT / "tools" / "cli" / "target" / "release" / "ceebase.exe"
CLI_DEBUG = REPO_ROOT / "tools" / "cli" / "target" / "debug" / "ceebase.exe"
CLI_BIN = str(CLI_RELEASE if CLI_RELEASE.exists() else CLI_DEBUG)


class TestCeebaseCLI(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cli = CLI_BIN
        cls.test_db = str(REPO_ROOT / "test_cli_runner.cbdb")
        cls.test_wal = cls.test_db + ".wal"
        cls.backup_db = str(REPO_ROOT / "backups" / "test_cli_runner_bkp.cbdb")
        cls.restored_db = str(REPO_ROOT / "test_cli_runner_restored.cbdb")

        # Cleanup any previous runs
        for p in [cls.test_db, cls.test_wal, cls.backup_db, cls.restored_db, cls.restored_db + ".wal"]:
            if os.path.exists(p):
                try:
                    os.remove(p)
                except Exception:
                    pass

    @classmethod
    def tearDownClass(cls):
        for p in [cls.test_db, cls.test_wal, cls.backup_db, cls.restored_db, cls.restored_db + ".wal"]:
            if os.path.exists(p):
                try:
                    os.remove(p)
                except Exception:
                    pass

    def run_cmd(self, args, expect_success=True):
        cmd = [self.cli] + args
        proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd=str(REPO_ROOT))
        if expect_success:
            self.assertEqual(proc.returncode, 0, f"Command failed: {cmd}\nStderr: {proc.stderr}\nStdout: {proc.stdout}")
        return proc

    def test_01_help_and_version(self):
        proc = self.run_cmd(["--help"])
        self.assertIn("Official CeeBase Database CLI", proc.stdout)
        self.assertIn("Commands:", proc.stdout)
        self.assertIn("init", proc.stdout)
        self.assertIn("query", proc.stdout)
        self.assertIn("inspect", proc.stdout)

        proc_ver = self.run_cmd(["--version"])
        self.assertIn("1.0.0", proc_ver.stdout)

    def test_02_init_and_inspect(self):
        # Init new database
        proc = self.run_cmd(["init", self.test_db])
        self.assertIn("Initialized fresh CeeBase database kernel storage", proc.stdout)
        self.assertTrue(os.path.exists(self.test_db))

        # Inspect initialized database
        proc_insp = self.run_cmd(["inspect", self.test_db])
        self.assertIn("CeeBase Storage Kernel Inspection", proc_insp.stdout)
        self.assertIn("Header Magic:", proc_insp.stdout)
        self.assertIn("CBDB - Valid", proc_insp.stdout)
        self.assertIn("Fixed Page Size:       4096 bytes", proc_insp.stdout)

    def test_03_query_ddl_and_dml(self):
        # Create table
        self.run_cmd(["query", "CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR, balance DOUBLE, active BOOLEAN);", "-d", self.test_db])

        # Insert rows
        self.run_cmd(["query", "INSERT INTO users VALUES (1, 'Alice', 1500.50, true);", "-d", self.test_db])
        self.run_cmd(["query", "INSERT INTO users VALUES (2, 'Bob', 2400.00, false);", "-d", self.test_db])

        # Select with Unicode borders
        proc_sel = self.run_cmd(["query", "SELECT * FROM users;", "-d", self.test_db])
        self.assertIn("Alice", proc_sel.stdout)
        self.assertIn("Bob", proc_sel.stdout)
        self.assertIn("1500.50", proc_sel.stdout)
        self.assertIn("2 rows in set", proc_sel.stdout)

    def test_04_query_plain_mode(self):
        # Plain ASCII table
        proc = self.run_cmd(["query", "SELECT * FROM users;", "-d", self.test_db, "--plain"])
        self.assertIn("+-----+-------+---------+--------+", proc.stdout)
        self.assertIn("| id  | name  | balance | active |", proc.stdout)
        self.assertIn("| 1   | Alice | 1500.50 | true   |", proc.stdout)

    def test_05_backup_and_restore(self):
        # Create backup
        proc_bkp = self.run_cmd(["backup", "-d", self.test_db, "--out", self.backup_db])
        self.assertIn("Backup completed successfully", proc_bkp.stdout)
        self.assertTrue(os.path.exists(self.backup_db))

        # Restore into another file
        proc_rst = self.run_cmd(["restore", self.backup_db, "-d", self.restored_db])
        self.assertIn("successfully restored from backup", proc_rst.stdout)
        self.assertTrue(os.path.exists(self.restored_db))

        # Query restored database to verify data integrity
        proc_verify = self.run_cmd(["query", "SELECT COUNT(*) FROM users;", "-d", self.restored_db])
        self.assertIn("2", proc_verify.stdout)

    def test_06_database_commands(self):
        # Create new database
        temp_name = "test_cli_db_cmd"
        self.run_cmd(["database", "create", temp_name])
        db_file = temp_name + ".cbdb"
        self.assertTrue(os.path.exists(db_file))

        # List databases
        proc_list = self.run_cmd(["database", "list"])
        self.assertIn(temp_name, proc_list.stdout)

        # Delete database
        self.run_cmd(["database", "delete", temp_name])
        self.assertFalse(os.path.exists(db_file))

    def test_07_user_and_organization_commands(self):
        proc_u = self.run_cmd(["user", "create", "lead_dev@ceebase.io", "--role", "admin"])
        self.assertIn("User 'lead_dev@ceebase.io' created successfully", proc_u.stdout)

        proc_o = self.run_cmd(["organization", "create", "Antigravity Cloud"])
        self.assertIn("Organization 'Antigravity Cloud' created successfully", proc_o.stdout)

    def test_08_config_get_and_set(self):
        self.run_cmd(["config", "--set", "cli_test_key=test_val_123"])
        proc_get = self.run_cmd(["config", "--get", "cli_test_key"])
        self.assertIn("test_val_123", proc_get.stdout)

    def test_09_error_handling(self):
        # Bad SQL syntax
        proc = self.run_cmd(["query", "SELECT * INVALID SQL SYNTAX;", "-d", self.test_db], expect_success=False)
        self.assertNotEqual(proc.returncode, 0)


if __name__ == "__main__":
    unittest.main()
