# Write-Ahead Logging & ARIES Crash Recovery

## Overview

**NexusDB** guarantees the **Atomicity** and **Durability** properties of ACID through Write-Ahead Logging (WAL) and an ARIES-style crash recovery engine.

---

## 1. Write-Ahead Logging Protocol

The WAL protocol enforces two fundamental invariants:
1. **The WAL Invariant (Physiological Logging)**: Before a dirty database page is written to persistent disk storage, all log records describing modifications to that page up to its Page LSN must be persisted to the WAL file.
2. **Durability on Commit**: Before a transaction is marked as committed, its `COMMIT` log record must be safely flushed to persistent storage (`fsync`).

---

## 2. Binary Log Record Layout

Every log record is serialized with canonical Little-Endian encoding and protected by an individual CRC-32 integrity checksum.

### Log Record Header (38 bytes)

| Field | Type | Size | Description |
| :--- | :--- | :--- | :--- |
| `record_size` | `uint32_t` | 4 B | Total length of this log record in bytes |
| `lsn` | `lsn_t` | 8 B | Unique monotonically increasing Log Sequence Number |
| `prev_lsn` | `lsn_t` | 8 B | Previous LSN of the same transaction (for Undo chains) |
| `txn_id` | `txn_id_t` | 8 B | ID of the transaction that generated this record |
| `type` | `uint16_t` | 2 B | Record type enum (BEGIN, COMMIT, ABORT, INSERT, UPDATE, DELETE, CHECKPOINT) |
| `table_name_len` | `uint16_t` | 2 B | Length of table name string |
| `page_id` | `page_id_t` | 4 B | Affected database page ID |
| `slot_id` | `slot_id_t` | 2 B | Affected slot ID within the page |

Following the header, variable-length fields are stored:
- `table_name`: UTF-8 encoded string.
- `before_image`: Bytes of the tuple before modification (used during Undo).
- `after_image`: Bytes of the tuple after modification (used during Redo).
- `crc32`: 4-byte CRC-32 checksum covering the entire record.

---

## 3. Crash Recovery Engine (`nexus::RecoveryEngine`)

Upon database startup or crash recovery invocation, the engine executes a classical three-phase ARIES recovery algorithm:

```
[WAL File on Disk]
       │
       ▼
 1. Analysis Phase   ───> Scan forward: identify active uncommitted txns & committed txns
       │
       ▼
 2. Redo Phase       ───> Scan forward: reapply all physical changes (repeating history)
       │
       ▼
 3. Undo Phase       ───> Scan backward: reverse changes of uncommitted "loser" txns
       │
       ▼
[Clean Database State]
```

### Phase 1: Analysis Phase
- Scans the WAL sequentially from the beginning (or latest checkpoint).
- Populates the Active Transaction Table (`active_txns_`) and Committed Transaction Table (`committed_txns_`).
- Any transaction with a `COMMIT` record is removed from `active_txns_` and marked committed.
- Any transaction still in `active_txns_` when the end of the log is reached was active when the system crashed.

### Phase 2: Redo Phase
- Replays all logged modifications forward to reconstruct the exact database state at the moment of the crash (repeating history).
- Reapplies after-images of `INSERT`, `UPDATE`, and `DELETE_REC` records.

### Phase 3: Undo Phase
- Traverses the log backwards from the end.
- Identifies records belonging to active (uncommitted) loser transactions.
- Reverses each operation:
  - Undoes `INSERT`: Marks the inserted tuple slot as deleted in the `TableHeap`.
  - Undoes `UPDATE`: Restores the original before-image.
  - Undoes `DELETE_REC`: Reinstates the deleted tuple.
- Once all uncommitted transactions are rolled back, the Buffer Pool Manager flushes all recovered pages to disk.
