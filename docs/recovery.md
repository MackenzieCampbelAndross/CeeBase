# Write-Ahead Logging & Crash Recovery

## Overview

**NexusDB** guarantees the **Atomicity** and **Durability** properties of ACID through Write-Ahead Logging (WAL) and an idempotent crash recovery engine.

---

## 1. Operating System Durability & Synchronization

Physical disk durability is achieved via OS-level hardware cache synchronization:
- **Windows**: `FlushFileBuffers((HANDLE)_get_osfhandle(_fileno(fp)))`
- **POSIX**: `fsync(fileno(fp))`

Both database file writes and WAL buffer writes are flushed through this protocol.

### Invariants Enforced
1. **The WAL Invariant**: Before a dirty database page is evicted from the `BufferPoolManager` or written to disk, all log records up to `page.GetLSN()` must be persisted to the WAL file via OS synchronization:
   $$\text{victim\_page.LSN} \le \text{log\_manager.PersistedLSN}$$
2. **Durability on Commit**: Before `TransactionManager::Commit(txn)` returns success, the `COMMIT` log record is appended to the log buffer and `LogManager::Flush()` synchronously invokes `FlushFileBuffers` / `fsync`.
3. **I/O Error Propagation**: All disk, buffer pool, and log synchronization failures are propagated without silent suppression.

---

## 2. Binary Log Record Layout

Every log record is canonically encoded in Little-Endian byte order:

| Field | Type | Size | Description |
| :--- | :--- | :--- | :--- |
| `lsn` | `uint64_t` | 8 B | Monotonically increasing Log Sequence Number |
| `prev_lsn` | `uint64_t` | 8 B | Previous LSN of the same transaction (for Undo chains) |
| `txn_id` | `uint64_t` | 8 B | ID of the transaction |
| `type` | `uint32_t` | 4 B | Record type enum (BEGIN, COMMIT, ABORT, INSERT, UPDATE, DELETE, CHECKPOINT) |
| `table_name` | `string` | Var | Table name |
| `page_id` | `uint32_t` | 4 B | Affected database page ID |
| `slot_id` | `uint16_t` | 2 B | Affected slot ID within the page |
| `before_image` | `bytes` | Var | Serialized tuple before modification (used during Undo) |
| `after_image` | `bytes` | Var | Serialized tuple after modification (used during Redo) |

---

## 3. Crash Recovery Engine (`nexus::RecoveryEngine`)

Upon database startup after an abnormal process termination or system crash, the recovery engine runs a three-phase recovery protocol:

```
[WAL File on Disk]
       │
       ▼
 1. Analysis Phase   ───> Scan forward: identify loser (active) & winner (committed) txns
       │
       ▼
 2. Redo Phase       ───> Scan forward: idempotent replay (if page_lsn < rec_lsn)
       │
       ▼
 3. Undo Phase       ───> Scan backward: reverse changes of loser transactions & update B+ Tree indexes
       │
       ▼
[Clean Database State]
```

### Phase 1: Analysis Phase
- Scans all log records from beginning to end.
- Populates `active_txns_` (uncommitted loser transactions) and `committed_txns_`.
- On `BEGIN`: records transaction ID.
- On `COMMIT` or `ABORT`: removes transaction from `active_txns_`.

### Phase 2: Redo Phase (Idempotent History Replay)
- Scans records sequentially.
- For each data record (`INSERT`, `UPDATE`, `DELETE_REC`):
  - Fetches the affected page `pid`.
  - **Idempotency check**: If `page->GetLSN() >= rec.GetLSN()`, the page already contains the logged change and was persisted before the crash. The operation is skipped!
  - If `page->GetLSN() < rec.GetLSN()`, the after-image is reapplied to the slotted page at its exact slot ID, `page->SetLSN(rec.GetLSN())` is updated, and the page is marked dirty.
  - Corresponding B+ Tree index entries are re-inserted or updated to maintain index consistency.

### Phase 3: Undo Phase
- Traverses the log backwards from the end.
- Identifies operations belonging to active (uncommitted) loser transactions.
- Reverses each operation:
  - Undoes `INSERT`: Marks the inserted tuple slot as deleted in the `TableHeap` and removes the key from associated B+ Tree indexes.
  - Undoes `UPDATE`: Re-writes the `before_image` to the slotted page slot and restores old index keys.
  - Undoes `DELETE_REC`: Re-writes the `before_image` to the slotted page slot and re-inserts the key into indexes.
- Appends `ABORT` log records for all undone transactions and flushes the log.
- Finally, invokes `BufferPoolManager::FlushAllPages()` to persist the clean state.

---

## 4. Recovery Guarantees & Transparency

- **Supported**:
  - Abrupt process crash (`ExitProcess`, `_exit`, `abort`, SIGKILL).
  - Uncommitted transaction rollback during recovery.
  - Repeating history during Redo with idempotent LSN checks.
  - Re-recovery idempotence (running recovery multiple times on the same log and database state).
  - Synchronization of all B+ Tree indexes during both Redo and Undo.
- **Architectural Scope**:
  - Single-writer architecture with serialized checkpointing.
  - Non-ARIES features omitted: NexusDB uses physiological undo without Compensation Log Records (CLRs). A crash during Undo requires restarting recovery from the checkpoint, which is safe and idempotent.
