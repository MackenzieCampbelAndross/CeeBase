# CeeBase Master Architectural Specification

CeeBase is an independent open-source database and Backend as a Service (BaaS) platform built around our own C++20 relational database engine. Supabase packages PostgreSQL into a developer backend platform; CeeBase packages CeeBase into a complete developer backend platform.

---

## 1. System Architecture

```text
                 DEVELOPER APPLICATIONS
                           │
          ┌────────────────┼────────────────┐
          │                │                │
     CEEBASE CLI     CEEBASE STUDIO   CEEBASE DESKTOP
   (Rust / Terminal)  (Web / Browser) (Native Window)
          │                │                │
          └────────────────┼────────────────┘
                           │
             OFFICIAL SDKS (@ceebase/sdk, Python)
                           │
                     API GATEWAY
               (/auth, /rest, /storage, /functions, /realtime)
                           │
          ┌────────────────┼────────────────┐
          │                │                │
    CONTROL PLANE     PROJECT SERVICES   MANAGEMENT
          │                │                │
    Organizations    Database API (REST) Monitoring
    Projects         Authentication      Audit Logs
    Provisioning     Authorization & RLS Metrics
    Credentials      Object Storage      Backups
    Metering         Realtime Changefeed Replication
                     Edge Functions
                           │
                     PROJECT ROUTER
                           │
               ┌───────────┼───────────┐
               │           │           │
           PROJECT A   PROJECT B   PROJECT C
               │           │           │
            CEEBASE     CEEBASE     CEEBASE
            DATABASE    DATABASE    DATABASE
          (Engine C++) (Engine C++) (Engine C++)
               │           │           │
               └───────────┼───────────┘
                           │
               INFRASTRUCTURE & PERSISTENCE
               (Slotted Pages, WAL, B+ Trees)
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
