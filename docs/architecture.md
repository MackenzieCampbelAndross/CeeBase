# CeeBase Architectural Specification

## 1. System Overview

CeeBase is an educational, production-grade relational database management system designed and built from first principles in C++20. It enforces rigorous subsystem isolation, deterministic byte-level disk serialization, zero external database engine dependencies, and zero third-party parser dependencies.

## 2. Layered Architecture

CeeBase is architected into cleanly decoupled vertical layers:

```
+-------------------------------------------------------+
|                 CLI / Interactive SQL Shell           |
+-------------------------------------------------------+
                           |
+-------------------------------------------------------+
|             Parser (Lexer, Parser, AST)               |
+-------------------------------------------------------+
                           |
+-------------------------------------------------------+
|            Planner (Logical & Physical Plans)         |
+-------------------------------------------------------+
                           |
+-------------------------------------------------------+
|        Execution Engine (Iterators / Volcano Model)   |
+-------------------------------------------------------+
        |                                       |
+----------------------+             +------------------+
|  Catalog Management  |             |  B+ Tree Index   |
+----------------------+             +------------------+
        \                                       /
         \                                     /
+-------------------------------------------------------+
|           Record Management (Slotted Pages / Heap)    |
+-------------------------------------------------------+
                           |
+-------------------------------------------------------+
|                 Buffer Pool Manager                   |
|           (Frame Table, Pinning, LRU Eviction)        |
+-------------------------------------------------------+
                           |
+-------------------------------------------------------+
|                 Disk Storage Engine                   |
|        (DiskManager, Fixed 4096-byte Page Slots)      |
+-------------------------------------------------------+
                           |
+-------------------------------------------------------+
|        Common (Types, Serialization, CRC32, Status)   |
+-------------------------------------------------------+
```

## 3. Subsystem Invariants & Core Design Principles

1. **Deterministic Wire & Disk Formats**:
   - In-memory C++ structures are NEVER directly dumped to disk.
   - All primitive data types are encoded in canonical Little-Endian representation via `ByteWriter` and `ByteReader`.
   - Structural padding and compiler alignment differences are completely eliminated from persistent artifacts.

2. **Explicit Error Semantics**:
   - Subsystems communicate via `Status` and `Result<T>` rather than unchecked runtime exceptions.
   - Any failure condition (e.g. `BufferPoolExhausted`, `Corruption`, `OutOfRange`) must be explicitly inspected at call sites.

3. **Memory Ownership & RAII**:
   - Explicit ownership models using `std::unique_ptr` and `std::shared_ptr`.
   - Buffer pool pages protected by RAII guards (`PageGuard`) preventing pin count leaks.

4. **Layered Verification**:
   - Every subsystem contains independent GoogleTest unit tests.
   - Integrity of storage blocks is validated via IEEE 802.3 CRC-32 checksums.
