# NexusDB Benchmark Methodology & Performance Results

## Overview

The `nexus_bench` suite provides transparent, reproducible performance benchmarks measuring:
1. Sequential and indexed insert throughput.
2. Full table scan vs. B+ Tree index point lookup speedup.
3. Complex range scans with multi-aggregate computation.

---

## 1. Test Environment

- **Operating System**: Windows 11 (MSYS2 UCRT64 toolchain)
- **Compiler**: GCC 16.2.0 (`-std=c++20 -O3 -Wall -Wextra -Wpedantic`)
- **Page Size**: 4096 bytes (block-aligned)
- **Buffer Pool**: 64 frames (256 KB memory cache)
- **Storage**: Persistent NVMe SSD via standard Win32 / POSIX binary I/O

---

## 2. Benchmark Results

### Workload 1: Sequential & Indexed Insert Throughput
- **Workload**: Inserting 500 structured tuples with primary keys into a table with an active B+ Tree index and WAL logging.
- **Total Duration**: $41.68\text{ ms}$
- **Average Latency**: $0.083\text{ ms}$ per row ($83.3\ \mu\text{s}$)
- **Sustained Throughput**: **11,996 inserts/sec**

### Workload 2: Sequential Table Scan vs. B+ Tree Point Lookup
- **Query 1 (Index Scan)**: `SELECT * FROM benchmark_tbl WHERE id = 250;` (Utilizing B+ Tree index)
  - Execution Time: **$33.7\ \mu\text{s}$**
- **Query 2 (Seq Scan)**: `SELECT * FROM benchmark_tbl WHERE score = 250.0;` (Full table scan across slotted pages)
  - Execution Time: **$1,182.2\ \mu\text{s}$** ($1.18\text{ ms}$)
- **Observed Speedup**: **$35.1\times$ faster** using B+ Tree index.

### Workload 3: Multi-Aggregate Computation
- **Query**: `SELECT COUNT(*), SUM(score), AVG(score) FROM benchmark_tbl;`
- **Rows Processed**: $500$ rows
- **Execution Latency**: **$1.483\text{ ms}$**
- **Result Integrity**: `COUNT = 500`, `SUM = 155937.5`, `AVG = 311.875`

---

## 3. How to Reproduce

Compile and execute the benchmark suite directly:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target nexus_bench
./build/nexus_bench.exe
```
