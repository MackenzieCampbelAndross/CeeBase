# NexusDB Benchmark Methodology & Performance Results

## Overview

The `nexus_bench` suite provides reproducible, transparent performance measurements across varying workload scales (500 and 10,000 records), reporting throughput, average latency, p50 latency, and p95 latency.

---

## 1. Test Environment

- **Operating System**: Windows 11 (Host: x86_64 Little-Endian)
- **Compiler**: GCC 16.2.0 (`-std=c++20 -O3 -Wall -Wextra -Wpedantic`)
- **Page Size**: 4096 bytes (4 KB fixed slotted pages)
- **Durability Mode**: Physical OS Synchronized WAL (`FlushFileBuffers` / `fsync`)
- **Storage**: Direct binary file I/O on NVMe SSD

---

## 2. Benchmark Results

### Scale 1: 500 Records (Buffer Pool: 64 frames / 256 KB)

| Operation | Throughput | Avg Latency | p50 Latency | p95 Latency |
| :--- | :--- | :--- | :--- | :--- |
| **Batch Sequential Insert** | 8,132 ops/s | 118.9 μs | 117.6 μs | 174.8 μs |
| **Point Lookup (B+ Tree)** | 31,923 ops/s | 31.2 μs | 29.9 μs | 36.3 μs |
| **Sequential Table Scan** | 893 ops/s | 1,119.0 μs | 1,116.0 μs | 1,135.4 μs |
| **Point Update (Indexed)** | 13,958 ops/s | 66.1 μs | 60.2 μs | 85.1 μs |
| **Point Delete (Indexed)** | 14,918 ops/s | 62.6 μs | 48.9 μs | 139.5 μs |

**Speedup**: B+ Tree point lookup was **35.9x faster** than full table scan.

---

### Scale 2: 10,000 Records (Buffer Pool: 1,000 frames / 4,000 KB)

| Operation | Throughput | Avg Latency | p50 Latency | p95 Latency |
| :--- | :--- | :--- | :--- | :--- |
| **Batch Sequential Insert** | 610 ops/s | 1,632.4 μs | 1,000.5 μs | 3,550.7 μs |
| **Point Lookup (B+ Tree)** | 13,455 ops/s | 73.9 μs | 73.0 μs | 109.0 μs |
| **Sequential Table Scan** | 22 ops/s | 46,002.6 μs | 45,426.9 μs | 49,786.3 μs |
| **Point Update (Indexed)** | 8,610 ops/s | 113.6 μs | 104.2 μs | 156.5 μs |
| **Point Delete (Indexed)** | 9,949 ops/s | 97.6 μs | 92.5 μs | 135.2 μs |

**Speedup**: B+ Tree point lookup was **622.6x faster** than full table scan.

---

## 3. How to Reproduce

Compile and execute the benchmark binary:

```bash
# Build release binaries
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target nexus_bench

# Run scale 500
./build/nexus_bench.exe 500

# Run scale 10,000
./build/nexus_bench.exe 10000
```
