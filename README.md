# NexusDB

**NexusDB** is a lightweight, educational relational database management system implemented from first principles in modern C++20.

It features persistent page-based storage, slotted pages, an explicit buffer pool manager with LRU eviction, a persistent B+ Tree index, an independent custom SQL lexer and parser, an iterator-based physical execution engine, Write-Ahead Logging (WAL), crash recovery, and an interactive command-line interface.

## Core Design Principles

- **Zero External Engine Dependencies**: Implements its own disk manager, buffer pool, B+ Tree, SQL parser, and execution engine from scratch.
- **Strict Byte-Level Serialization**: No raw C++ struct dumps to disk. All data is serialized in canonical Little-Endian byte order with explicit bounds checking.
- **Robust Error Handling**: Explicit `Status` and `Result<T>` error monads instead of unchecked runtime exceptions.
- **Fixed 4 KB Pages**: Block-aligned 4096-byte pages protected by IEEE 802.3 CRC-32 integrity checksums.
- **Modern C++20**: Utilizing `std::span`, `std::bit_cast`, `<concepts>`, `<ranges>`, and strict RAII memory management.

## Project Structure

```
NexusDB/
├── .github/workflows/    # CI pipelines (Linux GCC, Windows MSVC)
├── docs/                 # Architecture, storage engine, and page layout specifications
├── include/nexus/        # Public API and subsystem headers
│   └── common/           # Core types, Status, Result, ByteWriter, ByteReader
├── src/                  # Subsystem implementations
│   ├── common/           # Status and byte serialization utilities
│   └── cli/              # Interactive CLI executable
└── tests/                # GoogleTest unit and integration test suite
    └── common/           # Common subsystem unit tests
```

## Building NexusDB

### Requirements
- C++20 compliant compiler (GCC 12+, Clang 15+, or MSVC 19.30+)
- CMake 3.20+
- Ninja or Make

### Build Steps

```bash
# Configure the build system with tests enabled
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DNEXUS_BUILD_TESTS=ON

# Compile the engine and test suite
cmake --build build

# Run unit tests
ctest --test-dir build --output-on-failure
```

### Running the CLI

```bash
./build/nexus info
./build/nexus create mydb
./build/nexus open mydb
```

## Development Roadmap

- [x] **Phase 1: Project Infrastructure & Common Subsystem** (Core types, `Status`, `Result<T>`, `ByteWriter`/`ByteReader`, CRC32, CMake, GoogleTest, CLI)
- [ ] **Phase 2: File Manager & Page Manager** (DiskManager, 4 KB block I/O, database file headers, free page tracking)
- [ ] **Phase 3: Buffer Pool & Page Eviction** (Frame table, pin/unpin guards, dirty page management, LRU replacer)
- [ ] **Phase 4: Slotted Pages & Record Management** (Table heap, slotted page format, tuple serialization, CRUD operations)
- [ ] **Phase 5: Persistent B+ Tree Indexing** (Node splitting, merging, redistribution, range scanning)
- [ ] **Phase 6: SQL Lexer, Parser & AST** (Recursive descent parser, SQL tokenization, AST generation)
- [ ] **Phase 7: Query Planning & Execution Engine** (Logical/physical plans, Volcano iterator model, EXPLAIN)
- [ ] **Phase 8: Transactions, WAL & Crash Recovery** (ARIES-style recovery, redo/undo logging, ACID semantics)
- [ ] **Phase 9: Benchmarks, Stress Testing & Release**
