# CeeBase Master Implementation Roadmap & Architecture Audit

**Status:** Production Readiness Engineering (Phase 1 In Progress)  
**Target Engine:** CeeBase C++20 Relational Database Engine  
**Kernel Architecture:** 4 KB Slotted-Page Storage, B+ Tree Indexing, WAL / ARIES Recovery, Embedded C ABI  

---

## 1. Executive Architecture Audit & Current State

A comprehensive kernel reconnaissance was conducted across all 18 subsystems of CeeBase.

| Subsystem | Source Path | Current Status | Critical Deficiencies & Verified Bugs |
| :--- | :--- | :--- | :--- |
| **1. Storage Engine** | `src/storage/`, `include/ceebase/storage/` | Functional | Fixed 4 KB blocks, CRC32 verification on header and slotted pages. Free list allocation is operational. |
| **2. Disk Manager** | `src/storage/disk_manager.cpp` | Deficient | `DatabaseHeader` reserves `catalog_root_page_id` but lacks setter/getter APIs; catalog page is never persisted or updated in page 0. |
| **3. Page Serialization** | `src/common/byte_utils.cpp` | Solid | Endian-safe reader/writer, IEEE 754 float/double, CRC32 checksums verified. |
| **4. Buffer Pool** | `src/buffer/` | Deficient | LRU replacer and `PageGuard` RAII work well, but `FlushAllPages()` return status is discarded during engine shutdown / `ceebase_close`. |
| **5. Slotted Pages** | `src/record/slotted_page.cpp` | Functional | Slot array, backward tuple growth, and `Defragment()` are implemented and tested. |
| **6. Table Heap** | `src/record/table_heap.cpp` | Functional | Multi-page doubly linked slotted-page chains. Supports CRUD and `TableIterator`. |
| **7. B+ Tree Index** | `src/index/b_plus_tree.cpp` | Deficient | Key-value indexing operational, but insertion and deletion return values (`Status`/`bool`) are ignored in `ExecutionEngine`, risking silent index corruption on failure. |
| **8. Catalog** | `src/catalog/catalog.cpp` | **Critical Defect** | **Purely in-memory.** Schemas and table metadata are stored in `tables_` unordered map and lost on process restart. No persistent catalog root decoding or reconstruction. |
| **9. SQL Lexer & Parser** | `src/parser/` | Incomplete | AST covers DDL/DML, recently added `IF [NOT] EXISTS`. Lacks rich expressions (`NULL` checks, `BETWEEN`, `IN`, string patterns) and multi-table `JOIN` execution. |
| **10. Query Planner** | `src/planner/` | Functional | Generates logical plan trees (`SeqScan`, `IndexScan`, `Filter`, `Projection`, `Limit`, `Aggregation`). |
| **11. Execution Engine** | `src/execution/` | Deficient | Executes iterator pipelines. `QueryResult` only stores strings, losing runtime data types. Ignores index mutation errors. |
| **12. Transaction Manager**| `src/concurrency/` | Functional | ACID coordination with undo logs. Transaction table and 2PL table latching. |
| **13. Write-Ahead Log** | `src/recovery/log_manager.cpp`| Solid | Append-only WAL, LSN tracking, synchronous disk flushes. |
| **14. Recovery Engine** | `src/recovery/recovery_engine.cpp` | Deficient | 3-phase ARIES recovery (Analysis, Redo, Undo). Callers (`ceebase_open`, CLI) discard recovery failure errors, violating WAL invariants. |
| **15. C API** | `src/api/ceebase_c.cpp` | Deficient | `ceebase_column_type` hardcoded to `CEEBASE_TYPE_TEXT`. Ignores recovery errors in `ceebase_open`. Lacks `ceebase_column_is_null`. |
| **16. CLI** | `src/cli/main.cpp` | Functional | ANSI colors, box drawing, REPL. Ignores recovery failure on startup. |
| **17. SDK Bindings** | `bindings/` (Python, Java, Node.js) | Functional | Bindings function, but inherit C API deficiencies (untyped columns, missing null checks). |
| **18. Tests and CI** | `.github/workflows/ci.yml`, `tests/` | Deficient | Windows CI uses Linux `\` line continuation in PowerShell step, failing builds. `CMakeLists.txt` missing `POSITION_INDEPENDENT_CODE ON` for Linux shared libraries. Zero tests for catalog persistence across restart. |

---

## 2. Eight-Phase Production Roadmap

### Phase 1: Core Engine Integrity & Persistent Catalog (Current Mission)
* **Goal:** Eliminate silent error discards, implement persistent schema catalog across process restarts, correct C ABI types, fix CI scripts, and establish regression suite.
* **Acceptance Criteria:**
  1. `POSITION_INDEPENDENT_CODE ON` enabled for `ceebase_core` in CMake.
  2. Windows CI PowerShell syntax fixed in `ci.yml`.
  3. `ceebase_open` and `main.cpp` halt and return errors on `recovery->Recover()` failure.
  4. `ceebase_close` validates and returns `FlushAllPages()` status.
  5. Index insert/remove errors in `ExecutionEngine` abort transaction with status.
  6. Persistent Catalog: Schemas, tables, and index metadata persisted to disk (root page tracked in Page 0).
  7. Process restart preserves all tables, schemas, and B+ Trees automatically.
  8. C API reports accurate data types (`INTEGER`, `BIGINT`, `DOUBLE`, `FLOAT`, `BOOLEAN`, `TEXT`, `NULL`) and provides `ceebase_column_is_null`.
  9. 100% test pass rate with new regression suites for restart persistence and C API.

### Phase 2: Relational Capabilities & Multi-Table Joins
* **Goal:** Full relational query capabilities.
* **Acceptance Criteria:**
  1. `INNER JOIN` and `LEFT JOIN` execution engine support (Nested Loop & Hash Join).
  2. Multi-table schema resolution in parser and planner.
  3. Foreign key metadata tracking and referential integrity checks.

### Phase 3: Secondary Indexes & Expression Engine
* **Goal:** Accelerated lookups on non-primary-key columns and rich SQL expressions.
* **Acceptance Criteria:**
  1. Persistent secondary B+ Tree indexes via `CREATE INDEX`.
  2. Secondary index updates and vacuum during `UPDATE` and `DELETE`.
  3. `LIKE '%pattern%'`, `IN (...)`, `BETWEEN ... AND ...` filtering.

### Phase 4: Multi-Version Concurrency & Advanced Transactions
* **Goal:** High concurrency without read-write blocking.
* **Acceptance Criteria:**
  1. Snapshot isolation or multi-reader single-writer WAL mode.
  2. Deadlock detection via wait-for graph or lock timeouts.
  3. Savepoints within transactions (`SAVEPOINT`, `ROLLBACK TO`).

### Phase 5: Storage Optimization & Defragmentation
* **Goal:** Minimal disk footprint and sustained high throughput.
* **Acceptance Criteria:**
  1. Automatic page compaction and space reclamation (`VACUUM`).
  2. Slotted-page overflow pages for large BLOB/TEXT fields.
  3. Buffer pool clock-sweep / 2Q replacer optimizations.

### Phase 6: Enterprise Tooling, Studio & Backups
* **Goal:** Enterprise operations and observability.
* **Acceptance Criteria:**
  1. Online hot backup snapshot command.
  2. Graphical query plan visualizer in CeeBase Studio.
  3. Integrity check tool (`ceebase check`) verifying all page CRC32s and B+ Tree structures.

### Phase 7: Production SDKs & Language Drivers
* **Goal:** Seamless integration across enterprise languages.
* **Acceptance Criteria:**
  1. Full Python DB-API 2.0 type marshaling and parameter binding.
  2. Java JDBC-compatible driver wrapper over Panama FFM.
  3. Node.js native Promise/async API with prepared parameter bindings.
  4. Swift package with C bridging header.

### Phase 8: Cloud, Replication & Distributed Architecture
* **Goal:** High availability and cloud readiness.
* **Acceptance Criteria:**
  1. Raft-based WAL replication for active-passive failover.
  2. Point-In-Time Recovery (PITR) from WAL archives.
  3. Read-replica streaming.

---

## 3. Verified Bugs & Root Cause Analysis

### Bug 1: Catalog Volatility Across Database Reopen
* **Location:** `src/catalog/catalog.cpp`
* **Root Cause:** Table and index metadata only existed in `std::unordered_map<std::string, std::unique_ptr<TableMetadata>> tables_`. No code serialized schema definitions to disk or read `header_.catalog_root_page_id`.
* **Impact:** Any table created via `CREATE TABLE` disappeared upon process termination.
* **Resolution Plan:**
  - Define a binary versioned format for catalog pages (`CatalogPage` with header, table count, serialized schemas, and index metadata).
  - Allocate catalog root page via `bpm_->NewPage()`.
  - Update `DiskManager::SetCatalogRootPageId()` and sync Page 0.
  - On `Catalog` initialization, if `catalog_root_page_id != INVALID_PAGE_ID`, load catalog page(s), decode schemas, open table heaps at `first_page_id`, and rebuild B+ Tree handles.

### Bug 2: Ignored Recovery Failures During Startup
* **Location:** `src/api/ceebase_c.cpp:85`, `src/cli/main.cpp:134-137`
* **Root Cause:** `(void)db->recovery->Recover();` and `main.cpp` printing a warning rather than aborting.
* **Impact:** Corrupted WAL or disk state proceeds silently, resulting in database corruption or reading torn data.
* **Resolution Plan:** Check `recovery->Recover()`. If `!s.ok()`, record error message, close database, and return error code immediately.

### Bug 3: Discarded Buffer Pool Flush Errors on Shutdown
* **Location:** `src/api/ceebase_c.cpp:102`, `src/buffer/buffer_pool_manager.cpp:20`
* **Root Cause:** `(void)db->bpm->FlushAllPages();` discards I/O error status.
* **Impact:** Unflushed dirty pages due to disk write failures report success to the user on close, leading to data loss.
* **Resolution Plan:** Capture `FlushAllPages()` status and propagate through `ceebase_close()`.

### Bug 4: Hardcoded Column Types in C API
* **Location:** `src/api/ceebase_c.cpp:204`
* **Root Cause:** `ceebase_column_type()` unconditionally returns `CEEBASE_TYPE_TEXT`.
* **Impact:** Callers in Python, Java, and Node.js cannot distinguish numbers, booleans, or text, forcing fragile string parsing.
* **Resolution Plan:** Propagate `std::vector<DataType> column_types` in `QueryResult`, map them to `CEEBASE_TYPE_*`, and expose `ceebase_column_is_null()`.

### Bug 5: Silent Index Mutation Failures
* **Location:** `src/execution/execution_engine.cpp:290, 588, 589, 710`
* **Root Cause:** `(void)idx_meta->index->Insert(...)` casts away the return value.
* **Impact:** Failed index insertions or deletions (e.g. out of memory, tree split failure) allow the table heap to diverge from the index without error.
* **Resolution Plan:** Check status/boolean; if failed, abort active transaction and return `QueryResult{false, "Index update failed: ..."}`.

### Bug 6: Windows CI PowerShell Line Continuation Failure
* **Location:** `.github/workflows/ci.yml:67`
* **Root Cause:** Used Unix `\` continuation character inside PowerShell runner on `windows-latest`.
* **Impact:** CI fails on Windows runner.
* **Resolution Plan:** Replace `\` with single-line syntax or PowerShell backtick (`` ` ``).

### Bug 7: Missing Position Independent Code (PIC) in CMake
* **Location:** `CMakeLists.txt`
* **Root Cause:** `ceebase_core` static library lacks `POSITION_INDEPENDENT_CODE ON`, which prevents building `ceebase_shared` on Linux x86_64.
* **Impact:** Shared library linking fails on modern Linux distributions.
* **Resolution Plan:** Add `set_target_properties(ceebase_core PROPERTIES POSITION_INDEPENDENT_CODE ON)`.
