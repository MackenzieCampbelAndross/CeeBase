# CeeBase v1.0.0 Known Architectural Boundaries & Limitations

In accordance with strict production engineering standards, this document transparently records the design boundaries and current limitations of CeeBase v1.0.0.

---

## 1. Storage & Indexing Boundaries

- **Primary Key Index Typing**:
  - The B+ Tree index implementation currently specializes in 32-bit and 64-bit integer keys (`INT PRIMARY KEY` / `BIGINT PRIMARY KEY`).
  - Tables requiring non-numeric unique keys (such as UUIDs, emails, or composite keys) should use a synthetic auto-incrementing / serial integer primary key and enforce string uniqueness at the application layer or surrogate ID column.
- **Fixed Page Size**:
  - Fixed at 4096 bytes per page.
  - Tuples exceeding ~4000 bytes span multiple pages via slotted overflow chains.

---

## 2. SQL Parser & Query Planner Boundaries

- **Single-Table Query Execution**:
  - The v1.0 Volcano-style iterator engine executes queries against single relations (`SELECT ... FROM <table> WHERE ...`).
  - Cross-table `JOIN` operations (`INNER JOIN`, `LEFT OUTER JOIN`) and correlated subqueries (`WHERE col IN (SELECT ...)`) are not supported in the v1.0 parser. Applications perform multi-table correlations via client-side joining.
- **Schema Alterations**:
  - `ALTER TABLE` (adding/dropping columns on existing tables) is not yet supported. Schema changes are managed via table migration (`CREATE TABLE`, copy, `DROP TABLE`).

---

## 3. Concurrency & Transactional Guarantees

- **Single-Writer Serialization**:
  - Write transactions acquire exclusive table and page latches during modification and WAL flushing.
  - Concurrent readers access pages under shared latches without blocking other readers.
  - High concurrency is achieved through fast in-memory buffer pool operations (sub-millisecond commit latency), but CeeBase is not designed for multi-master concurrent writing.

---

## 4. Distributed Topology Boundaries

- **Active-Passive Replication**:
  - Replication operates in an **Active-Passive Primary/Standby** model via WAL streaming and log replay.
  - The Standby replica is strictly read-only (`ReadOnlyReplica`).
  - Active-Active multi-master write reconciliation is outside the scope of CeeBase v1.0.

---

## 5. Security & Tenant Boundaries

- **Local Break-Glass Console**:
  - Emergency break-glass administration is restricted strictly to local process execution on the server host.
  - It is never exposed over network TCP, REST, or MCP endpoints.
