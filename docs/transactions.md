# Transaction Management & Concurrency Control

## Overview

**NexusDB** provides transactional guarantees through a dedicated `TransactionManager` coordinating with the `LogManager` (Write-Ahead Logging) and `BufferPoolManager`.

---

## 1. Transaction Lifecycle & States

Every transaction proceeds through well-defined lifecycle states:

```
          ┌─────────────┐
          │   GROWING   │ <── BEGIN / START TRANSACTION
          └──────┬──────┘
                 │
        ┌────────┴────────┐
        ▼                 ▼
  ┌───────────┐     ┌───────────┐
  │ COMMITTED │     │  ABORTED  │
  └───────────┘     └───────────┘
```

1. **`GROWING`**: The transaction is active and modifying database state. Log records are appended to the WAL buffer.
2. **`COMMITTED`**: A `COMMIT` log record has been serialized and flushed to disk (`fsync`). All modifications are guaranteed to be durable.
3. **`ABORTED`**: The transaction encountered an error or invoked `ROLLBACK`. Any uncommitted inserts, updates, or deletes are systematically rolled back using recorded before-images.

---

## 2. Transaction Manager Architecture

```cpp
class TransactionManager {
public:
    std::unique_ptr<Transaction> Begin();
    Status Commit(Transaction* txn);
    Status Rollback(Transaction* txn);
};
```

### Invariants Maintained:
- **Transaction Identifiers (`txn_id_t`)**: Monotonically increasing unique 64-bit integers.
- **Undo Log Records**: In-flight modifications store undo information (such as previously deleted tuples or before-images of modified tuples).
- **Synchronous WAL Flushing on Commit**: When a transaction commits, the `LogManager` synchronously flushes all log records up to the commit LSN to disk before returning success to the client.

---

## 3. SQL Transaction Interface

Transactions are fully controllable via interactive and programmatic SQL commands:

```sql
nexus> BEGIN;
Query OK, 0 row(s) affected (0.012 ms)

nexus> INSERT INTO accounts VALUES (1, 'Alice', 5000.0);
Query OK, 1 row(s) affected (0.045 ms)

nexus> UPDATE accounts SET balance = 4500.0 WHERE id = 1;
Query OK, 1 row(s) affected (0.038 ms)

nexus> ROLLBACK;
Query OK, 0 row(s) affected (0.021 ms)

nexus> SELECT * FROM accounts;
0 row(s) in set (0.019 ms)
```

### Single-Writer Concurrency Control
NexusDB currently enforces a robust single-writer / multiple-reader model protected by fine-grained component latches across the catalog, buffer pool, disk manager, and table heaps, preventing concurrent race conditions and data corruption.
