# CeeBase Database Migration & Upgrade Guide

This guide describes version compatibility policies, database file format evolution, automated pre-migration snapshots, and disaster rollback workflows.

---

## 1. Storage Format Evolution

| Format Version | Magic Bytes | Page Layout | Features |
| :--- | :--- | :--- | :--- |
| **v0.9.0 (Legacy)** | `0x4E584442` (`NXDB`) | Unchecked 4 KB | Initial slotted pages, uncheckpointed catalog |
| **v1.0.0 (Production)** | `0x43424442` (`CBDB`) | Checked 4 KB + CRC-32 | Persistent catalog root, CRC-32 page validation, ARIES recovery, multi-tenant scoping |

---

## 2. Recoverable Migration Architecture

CeeBase guarantees that **no database file is ever upgraded without a recoverable rollback snapshot**:

1. **Pre-Flight Version Inspection**:
   Reads Page 0 header magic and version numbers to detect source version.
2. **Mandatory Snapshot**:
   Creates an exact, verified binary copy of the database and accompanying `.wal` log into `backups/migrations/pre_migrate_<db>_<timestamp>.cbdb`.
3. **Transactional Migration**:
   Executes schema updates and page reformatting.
4. **Header Stamping**:
   Overwrites magic bytes to `0x43424442` (`CBDB`), major version to `1`, minor version to `0`, and recalculates CRC-32 across bytes 0..4091.
5. **Atomic Rollback on Error**:
   If any migration statement fails, the engine catches the exception, deletes the partially mutated file, and atomically restores the pre-migration snapshot.

---

## 3. Running Database Migrations

### Programmatic Python Migration
```python
from src.cloud.migration_engine import MigrationEngine

# Initialize migration engine for target database
engine = MigrationEngine("production.cbdb")

# Detect current version
print("Current format version:", engine.detect_file_version())

# Execute migration to v1.0.0
result = engine.execute_migration(target_version="1.0.0")

if result["success"]:
    print("Migration successful! Snapshot stored at:", result["backup_snapshot"])
else:
    print("Migration failed! Rolled back to:", result["restored_from"])
```

---

## 4. Client SDK Compatibility Matrix

| Client Interface | Supported Engine Versions | Wire Protocol Version |
| :--- | :--- | :--- |
| **Rust CLI / TUI** | CeeBase v1.0.x | v1.0 |
| **Python DB-API (`ceebase`)** | CeeBase v1.0.x | v1.0 |
| **Node.js (`@ceebase/client`)** | CeeBase v1.0.x | v1.0 |
| **Java SDK (Panama FFM)** | CeeBase v1.0.x | v1.0 |
| **MCP Server (`mcp/server.py`)** | CeeBase v1.0.x | MCP 2024-11-05 |
| **REST API (`/api/v1/`)** | CeeBase v1.0.x | REST v1 |
