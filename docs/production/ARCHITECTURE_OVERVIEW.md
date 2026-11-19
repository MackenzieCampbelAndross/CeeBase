# CeeBase Master Architectural Specification

CeeBase is a high-performance, ACID-compliant database platform written in modern C++20 with a modular architecture supporting embedded local usage, network server access, multi-tenant cloud hosting, and AI agent integration.

---

## 1. System Architecture

```
┌────────────────────────────────────────────────────────────────────────┐
│                        CeeBase Developer Interfaces                    │
│   Rust CLI (TUI)  │  Python DB-API  │  Node.js FFI  │  Java FFM  │ MCP │
└─────────┬───────────────────┬───────────────┬─────────────┬───────┬────┘
          │                   │               │             │       │
          ▼                   ▼               ▼             ▼       ▼
┌──────────────────┐  ┌──────────────────────────────────────────────────┐
│ Cloud Control    │  │                   C API                          │
│ Plane (Managed)  │  │   (ceebase_open, ceebase_prepare, ceebase_step)   │
└─────────┬────────┘  └───────────────────────┬──────────────────────────┘
          │                                   │
          ▼                                   ▼
┌────────────────────────────────────────────────────────────────────────┐
│                        CeeBase Engine Kernel                           │
│                                                                        │
│   ┌─────────────────────┐                 ┌────────────────────────┐   │
│   │   SQL Lexer &       │                 │   Execution Engine     │   │
│   │   Recursive Parser  │ ──────────────> │   (Volcano Iterator)   │   │
│   └─────────────────────┘                 └───────────┬────────────┘   │
│                                                       │                │
│   ┌─────────────────────┐                 ┌───────────▼────────────┐   │
│   │  Catalog Manager    │                 │   Table Heap &         │   │
│   │  (Persistent Root)  │                 │   B+ Tree Indexes      │   │
│   └─────────────────────┘                 └───────────┬────────────┘   │
│                                                       │                │
│   ┌─────────────────────┐                 ┌───────────▼────────────┐   │
│   │ Transaction Manager │                 │   Buffer Pool Manager  │   │
│   │ & Recovery Engine   │ <─────────────> │   (LRU Page Replacer)  │   │
│   └──────────┬──────────┘                 └───────────┬────────────┘   │
│              │                                        │                │
│              ▼                                        ▼                │
│   ┌─────────────────────┐                 ┌────────────────────────┐   │
│   │ Log Manager (WAL)   │                 │   Disk Manager         │   │
│   │ (Append-Only Log)   │                 │   (Fixed 4096-B Pages) │   │
│   └─────────────────────┘                 └────────────────────────┘   │
└────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Core Kernel Components

### 2.1 Storage Engine & Disk Manager
- **Page Layout**: Fixed 4096-byte slotted pages (`SlottedPage`).
- **Page 0 (Header Page)**: Contains database header magic (`0x43424442` "CBDB"), format version (Major/Minor), total page count, free page list head, catalog root page ID, and CRC-32 checksum (stored in bytes 4092–4095).
- **Free Page Management**: Deallocated pages form a singly-linked free page list, immediately reused upon allocation to prevent physical storage bloat.

### 2.2 Buffer Pool Manager
- **Configurable Frame Pool**: Manages in-memory frames (default 16,384 pages = 64 MB) using an LRU replacement policy (`LRUReplacer`).
- **Thread Safety & Page Guards**: Thread-safe page latching (`std::shared_mutex`) with RAII `PageGuard` ensuring pins are decremented upon scope exit.

### 2.3 Table Heap & Record Storage
- **Slotted Pages**: Stores variable-length and fixed-length tuples. Slots grow forwards from the header, while tuple records grow backwards from the bottom of the page.
- **Defragmentation**: Automatic page compaction upon tuple deletion or update.

### 2.4 Indexing (B+ Tree)
- High-fanout B+ Tree index supporting exact match and range scans.
- Primary key index strictly enforces uniqueness with logarithmic search complexity $\mathcal{O}(\log N)$.

### 2.5 Transactions, WAL & Crash Recovery
- **Write-Ahead Logging (WAL)**: Strict Write-Ahead Logging protocol: WAL records are flushed to disk before any dirty data page is written.
- **ARIES-Style Crash Recovery**:
  - Analysis Phase: Identifies dirty pages and active transactions.
  - Redo Phase: Replays committed log records to restore page state.
  - Undo Phase: Rolls back active, uncommitted transactions.

---

## 3. Control Plane vs Data Plane Separation

- **Data Plane**: Always self-contained. The open-source C++ engine, Rust CLI, and client SDKs execute queries, manage storage, and enforce transactions with zero external cloud dependencies.
- **Control Plane**: Independent management layer responsible for tenant provisioning, quota enforcement, usage metering, and credential rotation.
