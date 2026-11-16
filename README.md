# NexusDB

[![C++20](https://img.shields.io/badge/standard-C%2B%2B20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![CMake](https://img.shields.io/badge/build-CMake%203.20%2B-orange.svg)](https://cmake.org)
[![Tests](https://img.shields.io/badge/tests-57%2F57%20passing-brightgreen.svg)]()
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)

**NexusDB** is a high-performance, educational relational database management system implemented from first principles in modern C++20.

It implements its own persistent storage engine, fixed 4 KB slotted database pages, buffer pool manager with LRU eviction and strict WAL invariant enforcement, persistent B+ Tree indexing, custom SQL lexer, recursive descent parser and AST, Volcano-iterator query execution engine, cost-based optimizer, Write-Ahead Logging (WAL) with operating-system level durable synchronization (`FlushFileBuffers` / `fsync`), idempotent crash recovery, and interactive CLI shell.

---

## Key Features

- **Zero External Engine Dependencies**: Implements disk I/O, buffer cache, B+ Tree, parser, and execution engine independently. No SQLite, PostgreSQL, or third-party engines.
- **Strict Binary Serialization**: Canonical Little-Endian byte-level encoding with explicit bounds checking and IEEE 802.3 CRC-32 integrity checksums on every 4 KB page block.
- **Slotted Pages & Variable-Length Records**: Slotted page layout supporting `INTEGER`, `BIGINT`, `FLOAT`, `DOUBLE`, `BOOLEAN`, and variable-length `TEXT`/`VARCHAR` with in-place updates, arbitrary slot restoration, and automatic defragmentation.
- **Configurable Buffer Pool**: Frame management with LRU page replacement, page pinning/unpinning, dirty page tracking, RAII `PageGuard`, and strict WAL invariant verification before dirty writes reach disk.
- **Persistent B+ Tree Indexing**: Disk-backed B+ Tree supporting point lookups ($O(\log N)$), node splitting, merging, redistribution, leaf pointer linking, and range scans.
- **Index Consistency**: Full synchronization across mutations—`INSERT`, `UPDATE`, and `DELETE` maintain index entries, handle primary key collision prevention, and rollback/recover cleanly.
- **SQL Lexer & Recursive Descent Parser**: Tokenizer and recursive descent parser supporting `CREATE TABLE`, `DROP TABLE`, `CREATE INDEX`, `INSERT`, `SELECT`, `UPDATE`, `DELETE`, `EXPLAIN`, `WHERE`, `AND`/`OR`, `ORDER BY`, `LIMIT`, `INNER JOIN ... ON`, `GROUP BY`, and aggregates (`COUNT`, `SUM`, `AVG`, `MIN`, `MAX`, `HAVING`).
- **Cost-Based Query Optimizer & Index Acceleration**: Access path selection, transforming sequential scans with equality filters into B+ Tree index scans, with EXPLAIN diagnostic output.
- **Physical OS Durability**: Synchronous disk synchronization via `FlushFileBuffers` on Windows and `fsync` on POSIX before commit completion.
- **Crash Recovery & Process-Based Crash Testing**: 3-phase idempotent recovery (Analysis, Redo, Undo) thoroughly verified by a dedicated multi-process crash test harness simulating abrupt process termination across 6 failure scenarios.
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
                       |   (LRU + WAL Invariant Flush)     |
                       +--------+------------------+-------+
                                |                  |
          Block Read / Write    |                  | WAL Append / OS Sync
                                v                  v
                       +--------+-------+  +-------+-------+
                       |  Disk Manager  |  |  Log Manager  |
                       |  (4 KB Pages)  |  |  (WAL File)   |
                       +----------------+  +---------------+
```

---

## Performance Benchmarks

Measured on AMD / Intel x86_64 running Windows 11 with GCC 16.2.0 (`-O3` Release) with OS-level durable WAL sync:

### 500 Records Workload (64 Frames / 256 KB Buffer Pool)

| Benchmark Workload | Throughput | Avg Latency | p50 Latency | p95 Latency |
| :--- | :--- | :--- | :--- | :--- |
| **Batch Sequential Insert** | **8,132 ops/s** | 118.9 μs | 117.6 μs | 174.8 μs |
| **Point Lookup (B+ Tree)** | **31,923 ops/s** | 31.2 μs | 29.9 μs | 36.3 μs |
| **Sequential Table Scan** | **893 ops/s** | 1,119.0 μs | 1,116.0 μs | 1,135.4 μs |
| **Point Update (Indexed)** | **13,958 ops/s** | 66.1 μs | 60.2 μs | 85.1 μs |
| **Point Delete (Indexed)** | **14,918 ops/s** | 62.6 μs | 48.9 μs | 139.5 μs |

*B+ Tree point lookup was **35.9x faster** than full sequential table scan.*

### 10,000 Records Workload (1,000 Frames / 4,000 KB Buffer Pool)

| Benchmark Workload | Throughput | Avg Latency | p50 Latency | p95 Latency |
| :--- | :--- | :--- | :--- | :--- |
| **Point Lookup (B+ Tree)** | **13,455 ops/s** | 73.9 μs | 73.0 μs | 109.0 μs |
| **Sequential Table Scan** | **22 ops/s** | 46,002.6 μs | 45,426.9 μs | 49,786.3 μs |
| **Point Update (Indexed)** | **8,610 ops/s** | 113.6 μs | 104.2 μs | 156.5 μs |
| **Point Delete (Indexed)** | **9,949 ops/s** | 97.6 μs | 92.5 μs | 135.2 μs |

*B+ Tree point lookup was **622.6x faster** than full sequential table scan.*

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

# Execute full automated test suite (57/57 tests)
./build/tests/nexus_tests.exe

# Execute performance benchmark suite
./build/nexus_bench.exe 500
./build/nexus_bench.exe 10000
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
- [Architectural Scope & Limitations](docs/limitations.md)

---

## License

NexusDB is released under the permissive [MIT License](LICENSE).
