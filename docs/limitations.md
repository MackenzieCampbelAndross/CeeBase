# NexusDB Architectural Limitations & Engineering Scope

This document provides transparent technical disclosure regarding the architectural design boundaries, trade-offs, and current engineering scope of **NexusDB v1.0**.

---

## 1. Concurrency Control Model
- **Single-Writer Serialization**: NexusDB uses a single-writer architecture coordinated via mutex synchronization in `TransactionManager`, `DiskManager`, and `BufferPoolManager`.
- **Planned Evolution**: Future versions will implement Strict Two-Phase Locking (SS2PL) or Multi-Version Concurrency Control (MVCC) with fine-grained page and tuple latches.

---

## 2. SQL Dialect & Execution Scope
- **Supported Operations**:
  - `CREATE TABLE`, `DROP TABLE`, `CREATE INDEX`
  - `INSERT INTO ... VALUES (...)`
  - `SELECT ... FROM ... [JOIN ...] [WHERE ...] [GROUP BY ... [HAVING ...]] [ORDER BY ...] [LIMIT ...]`
  - `UPDATE ... SET ... WHERE ...`
  - `DELETE FROM ... WHERE ...`
  - `BEGIN`, `COMMIT`, `ROLLBACK`
  - `EXPLAIN SELECT ...`
- **Current Constraints**:
  - Subqueries and Common Table Expressions (CTEs) are not supported.
  - Window functions and triggers are outside the v1.0 scope.
  - Joins are executed using a Nested-Loop Join (NLJ) operator. Hash joins and sort-merge joins are not yet implemented.

---

## 3. Indexing Subsystem
- **Key Types**: B+ Tree indexing is optimized for 64-bit integer values (`INTEGER` and `BIGINT`).
- **Composite Indexes**: Multi-column composite keys are not currently supported; each B+ Tree index indexes a single column.
- **Unique Constraints**: Automatically enforced for primary key columns via the primary key B+ Tree index.

---

## 4. Crash Recovery Model
- **ARIES-Style Recovery**: Uses a 3-phase Analysis, Redo, and Undo protocol with monotonic LSN tracking and physical OS synchronization (`FlushFileBuffers` on Windows, `fsync` on POSIX).
- **Undo Strategy**: Physiological undo reverses operations directly on slotted pages without generating Compensation Log Records (CLRs). A crash during the Undo phase safely repeats recovery from the latest checkpoint or log start.

---

## 5. Catalog Persistence
- **Runtime Schema**: The `Catalog` holds table metadata, schemas, and active indexes in memory. On database restart or recovery across process boundaries, table schemas are registered into the catalog using `Catalog::RegisterExistingTable`. Persistent catalog catalog root page decoding is planned for v1.1.
