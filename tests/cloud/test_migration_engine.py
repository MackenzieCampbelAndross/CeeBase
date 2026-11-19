#!/usr/bin/env python3
"""
Automated Test Suite for CeeBase Migration & Upgrade Engine
Tests:
- Header format version detection.
- Pre-migration backup snapshotting.
- Successful schema/format upgrade.
- Automatic atomic rollback on migration error.
"""

import os
import shutil
import struct
import sys
import unittest
import zlib
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(REPO_ROOT))
sys.path.insert(0, str(REPO_ROOT / "bindings" / "python"))

import ceebase
from src.cloud.migration_engine import MigrationEngine, MigrationStep


class TestMigrationEngine(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.test_dir = REPO_ROOT / "test_migration_sandbox"
        cls.test_dir.mkdir(exist_ok=True)

    @classmethod
    def tearDownClass(cls):
        if cls.test_dir.exists():
            shutil.rmtree(cls.test_dir, ignore_errors=True)

    def test_01_version_detection(self):
        db_path = str(self.test_dir / "ver_detect.cbdb")
        conn = ceebase.connect(db_path)
        conn.close()

        engine = MigrationEngine(db_path, str(self.test_dir / "backups"))
        v = engine.detect_file_version()
        self.assertEqual(v, "1.0.0")

    def test_02_pre_migration_backup_and_upgrade(self):
        db_path = str(self.test_dir / "upgrade_test.cbdb")
        conn = ceebase.connect(db_path)
        conn.execute("CREATE TABLE legacy_items (id INT PRIMARY KEY, title VARCHAR);")
        conn.execute("INSERT INTO legacy_items VALUES (1, 'Legacy Box');")
        conn.close()

        # Simulate legacy v0.9 header magic (0x4E584442 "NXDB") with valid CRC32
        with open(db_path, "r+b") as f:
            raw = bytearray(f.read(4096))
            raw[0:4] = struct.pack("<I", 0x4E584442)
            raw[4:6] = struct.pack("<H", 0)
            raw[6:8] = struct.pack("<H", 9)
            crc = zlib.crc32(raw[:4092])
            raw[4092:4096] = struct.pack("<I", crc)
            f.seek(0)
            f.write(raw)

        engine = MigrationEngine(db_path, str(self.test_dir / "backups"))
        self.assertEqual(engine.detect_file_version(), "0.9.0")

        # Execute migration
        res = engine.execute_migration(target_version="1.0.0")
        self.assertTrue(res["success"])
        self.assertTrue(res["migrated"])
        self.assertEqual(res["to_version"], "1.0.0")
        self.assertTrue(os.path.exists(res["backup_snapshot"]))

        # Check that database now reports 1.0.0 and preserved table data
        self.assertEqual(engine.detect_file_version(), "1.0.0")
        conn_up = ceebase.connect(db_path)
        cur = conn_up.cursor()
        cur.execute("SELECT title FROM legacy_items WHERE id = 1;")
        self.assertEqual(cur.fetchone()[0], "Legacy Box")
        conn_up.close()

    def test_03_automatic_rollback_on_failure(self):
        db_path = str(self.test_dir / "rollback_test.cbdb")
        conn = ceebase.connect(db_path)
        conn.execute("CREATE TABLE test_rollback (id INT PRIMARY KEY);")
        conn.execute("INSERT INTO test_rollback VALUES (42);")
        conn.close()

        # Set fake version with valid CRC32 to trigger migration
        with open(db_path, "r+b") as f:
            raw = bytearray(f.read(4096))
            raw[0:4] = struct.pack("<I", 0x4E584442)
            raw[4:6] = struct.pack("<H", 0)
            raw[6:8] = struct.pack("<H", 9)
            crc = zlib.crc32(raw[:4092])
            raw[4092:4096] = struct.pack("<I", crc)
            f.seek(0)
            f.write(raw)

        engine = MigrationEngine(db_path, str(self.test_dir / "backups"))

        # Inject broken migration step that raises an error midway
        def broken_handler(c):
            c.execute("INVALID SQL STATEMENT THAT FAILS;")

        engine.registered_migrations = [
            MigrationStep("0.9.0", "1.0.0", "Failing step", broken_handler)
        ]

        res = engine.execute_migration("1.0.0")
        self.assertFalse(res["success"])
        self.assertTrue(res["rollback_performed"])

        # Check that database rolled back to pre-migration state and data is intact
        conn_rb = ceebase.connect(db_path)
        cur = conn_rb.cursor()
        cur.execute("SELECT * FROM test_rollback;")
        row = cur.fetchone()
        self.assertEqual(row[0], 42)
        conn_rb.close()


if __name__ == "__main__":
    unittest.main()
