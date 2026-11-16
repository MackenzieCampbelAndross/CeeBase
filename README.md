# NexusDB

[![C++20](https://img.shields.io/badge/standard-C%2B%2B20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![CMake](https://img.shields.io/badge/build-CMake%203.20%2B-orange.svg)](https://cmake.org)
[![Tests](https://img.shields.io/badge/tests-45%2F45%20passing-brightgreen.svg)]()
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)

**NexusDB** is a lightweight, educational relational database management system implemented from first principles in modern C++20.

It implements its own persistent storage engine, fixed 4 KB slotted database pages, buffer pool manager with LRU eviction, persistent B+ Tree indexing, custom SQL lexer, recursive descent parser and AST, Volcano-iterator query execution engine, cost-based optimizer, Write-Ahead Logging (WAL), ARIES crash recovery, and interactive CLI shell.

---

## Key Features

- **Zero External Engine Dependencies**: Implements disk I/O, buffer cache, B+ Tree, parser, and execution engine independently. No SQLite, PostgreSQL, or third-party engines.
- **Strict Binary Serialization**: Canonical Little-Endian byte-level encoding with explicit bounds checking and IEEE 802.3 CRC-32 integrity checksums on every 4 KB page block.
- **Slotted Pages & Variable-Length Records**: Slotted page layout supporting `INTEGER`, `BIGINT`, `FLOAT`, `DOUBLE`, `BOOLEAN`, and variable-length `TEXT`/`VARCHAR` with in-place updates and automatic defragmentation.
- **Configurable Buffer Pool**: Frame management with LRU page replacement, page pinning/unpinning, dirty page tracking, and RAII `PageGuard`.
- **Persistent B+ Tree Indexing**: Disk-backed B+ Tree supporting point lookups ($O(\log N)$), node splitting, merging, redistribution, leaf pointer linking, and range scans.
- **SQL Lexer & Recursive Descent Parser**: Tokenizer and recursive descent parser supporting `CREATE TABLE`, `DROP TABLE`, `CREATE INDEX`, `INSERT`, `SELECT`, `UPDATE`, `DELETE`, `EXPLAIN`, `WHERE`, `AND`/`OR`, `ORDER BY`, `LIMIT`, `INNER JOIN ... ON`, `GROUP BY`, and aggregates (`COUNT`, `SUM`, `AVG`, `MIN`, `MAX`, `HAVING`).
- **Cost-Based Query Optimizer**: Access path selection, transforming sequential scans with equality filters into B+ Tree index scans, with EXPLAIN diagnostic output.
- **ACID Transactions & Write-Ahead Logging**: Physiological WAL with log sequence numbers (LSNs), before/after images, and commit durability.
- **ARIES-Style Crash Recovery**: 3-phase recovery algorithm (Analysis, Redo, Undo) capable of rolling back uncommitted loser transactions after a simulated crash.
- **Interactive Command-Line Interface**: Terminal shell with ASCII table formatting, timing statistics, and database lifecycle management.

---

## Subsystem Architecture

```
                       +-----------------------------------+
                       |         Interactive CLI           |
                       +-----------------+-----------------+
                                         | SQL Query
                                         v
                       +-----------------+-----------------+
                       |            SQL Lexer              |
                       +-----------------+-----------------+
                                         | Token Stream
                                         v
                       +-----------------+-----------------+
                       |     Recursive Descent Parser      |
                       +-----------------+-----------------+
                                         | Abstract Syntax Tree (AST)
                                         v
                       +-----------------+-----------------+
                       |       Planner & Optimizer         |
                       +-----------------+-----------------+
                                         | Physical Plan Tree
                                         v
                       +-----------------+-----------------+
                       |    Volcano Execution Engine       |
                       |  (SeqScan, IndexScan, Filter,     |
                       |   Sort, Join, Aggregate, Limit)   |
                       +--------+------------------+-------+
                                |                  |
          Tuple Heap Record I/O |                  | Key Lookup / Insert
                                v                  v
                       +--------+-------+  +-------+-------+
                       |   TableHeap    |  |  B+ Tree Index|
                       | (Slotted Pages)|  | (Internal/Leaf|
                       +--------+-------+  +-------+-------+
                                |                  |
                                +--------+---------+
                                         | Page Fetch / Pin
                                         v
                       +-----------------+-----------------+
                       |       Buffer Pool Manager         |
                       |          (LRU Eviction)           |
                       +--------+------------------+-------+
                                |                  |
          Block Read / Write    |                  | WAL Append / Flush
                                v                  v
                       +--------+-------+  +-------+-------+
                       |  Disk Manager  |  |  Log Manager  |
                       |  (4 KB Pages)  |  |  (WAL File)   |
                       +----------------+  +---------------+
```

---

## Performance Benchmarks

Measured on AMD / Intel x86_64 running Windows 11 with MSYS2 GCC 16.2.0 (`-O3` Release):

| Benchmark Workload | Result / Throughput | Notes |
| :--- | :--- | :--- |
| **Sequential & Indexed Inserts** | **11,996 inserts/sec** | 500 rows, avg latency $83.3\ \mu\text{s}$/insert |
| **B+ Tree Point Lookup** | **$33.7\ \mu\text{s}$** | $O(\log N)$ root-to-leaf traversal |
| **Full Table Scan (500 rows)** | **$1,182.2\ \mu\text{s}$** | Slotted page block scan |
| **Index Speedup Factor** | **$35.1\times$ faster** | Speedup over full table scan |
| **Multi-Aggregate Query** | **$1.483\text{ ms}$** | `COUNT(*)`, `SUM(score)`, `AVG(score)` |

---

## Building and Testing

### Prerequisites
- C++20 compliant compiler (GCC 12+, Clang 15+, or MSVC 2022)
- CMake 3.20+
- Ninja or Make

### Build Instructions

```bash
# Clone the repository
git clone https://github.com/MackenzieCampbelAndross/NexusDB.git
cd NexusDB

# Configure build with tests and benchmarks enabled
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DNEXUS_BUILD_TESTS=ON

# Compile everything
cmake --build build

# Execute full automated test suite (45/45 tests)
./build/tests/nexus_tests.exe

# Execute performance benchmark suite
./build/nexus_bench.exe
```

---

## Interactive CLI Walkthrough

Launch the database CLI:

```bash
# 1. Create a new persistent database
./build/nexus.exe create company.nxdb

# 2. Open interactive shell
./build/nexus.exe open company.nxdb
```

Within the shell:

```sql
-- Create table with primary key index
nexus> CREATE TABLE employees (id INT PRIMARY KEY, name TEXT, salary DOUBLE);
Query OK, 0 row(s) affected (0.245 ms)

-- Multi-row insert
nexus> INSERT INTO employees VALUES (1, 'Ada Lovelace', 120000.0), (2, 'Alan Turing', 125000.0), (3, 'Claude Shannon', 115000.0);
Query OK, 3 row(s) affected (0.312 ms)

-- Query all records
nexus> SELECT * FROM employees;
+----+----------------+---------------+
| id | name           | salary        |
+----+----------------+---------------+
| 1  | Ada Lovelace   | 120000.000000 |
| 2  | Alan Turing    | 125000.000000 |
| 3  | Claude Shannon | 115000.000000 |
+----+----------------+---------------+
3 row(s) in set (0.082 ms)

-- Inspect query optimizer execution plan
nexus> EXPLAIN SELECT * FROM employees WHERE id = 1;
=== NexusDB Query Execution Plan ===

[1. Logical Query Plan]:
  -> Filter [Predicate: (id = 1)]
    -> SeqScan [Table: employees]

[2. Physical Execution Plan]:
  -> IndexScan [Table: employees, Index: employees_pk_idx, Key: id = 1]

[3. Optimizer Diagnostics]:
  Selected Access Path: IndexScan [Index: employees_pk_idx, Key: id = 1]
  Index Used:           YES
  Estimated I/O Cost:   3
  Est. Cardinality:     1 row(s)

Execution time: 0.071 ms

-- Aggregations
nexus> SELECT COUNT(*), SUM(salary), AVG(salary) FROM employees;
+----------+---------------+---------------+
| COUNT(*) | SUM(salary)   | AVG(salary)   |
+----------+---------------+---------------+
| 3        | 360000.000000 | 120000.000000 |
+----------+---------------+---------------+
1 row(s) in set (0.104 ms)

-- Transactions & Rollback
nexus> BEGIN;
Query OK, 0 row(s) affected (0.010 ms)

nexus> DELETE FROM employees WHERE id = 1;
Query OK, 1 row(s) affected (0.048 ms)

nexus> ROLLBACK;
Query OK, 0 row(s) affected (0.025 ms)

nexus> SELECT COUNT(*) FROM employees;
+----------+
| COUNT(*) |
+----------+
| 3        |
+----------+
1 row(s) in set (0.035 ms)

nexus> exit;
```

---

## Technical Documentation

- [Architecture Overview](docs/architecture.md)
- [Page Binary Layout](docs/page-layout.md)
- [Storage Subsystem](docs/storage-engine.md)
- [Persistent B+ Tree Index](docs/btree.md)
- [SQL Lexer, Parser & AST](docs/sql-processing.md)
- [Query Planning, Optimization & Execution](docs/query-execution.md)
- [Transaction Management](docs/transactions.md)
- [Write-Ahead Logging & Crash Recovery](docs/recovery.md)
- [Benchmark Methodology & Results](docs/benchmarks.md)

---

## License

NexusDB is released under the permissive [MIT License](LICENSE).
