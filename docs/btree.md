# Persistent B+ Tree Index Specification

## Overview

**NexusDB** implements a fully persistent, page-backed B+ Tree index designed for high-performance primary and secondary key lookups, range scans, and ordered iteration.

The B+ Tree is integrated directly with the Buffer Pool Manager: all nodes (internal and leaf) are allocated as fixed 4096-byte database pages (`BTREE_INTERNAL = 0x0004` and `BTREE_LEAF = 0x0005`).

---

## 1. Page Layout & Binary Encoding

Every B+ Tree page starts with a common header, followed by typed entry arrays.

### Common B+ Tree Header (28 bytes)

| Field | Type | Size | Description |
| :--- | :--- | :--- | :--- |
| `page_type` | `uint16_t` | 2 B | `0x0004` (Internal) or `0x0005` (Leaf) |
| `lsn` | `uint64_t` | 8 B | Log Sequence Number for WAL recovery |
| `size` | `int32_t` | 4 B | Current number of active entries in the node |
| `max_size` | `int32_t` | 4 B | Maximum entry capacity before splitting |
| `parent_page_id` | `page_id_t` | 4 B | Page ID of parent (`INVALID_PAGE_ID` if root) |
| `page_id` | `page_id_t` | 4 B | Page ID of this node |

### Leaf Page Layout

In addition to the common header, leaf pages maintain forward and backward sibling pointers for bidirectional $O(1)$ sequential range scans:

- `next_page_id_`: `page_id_t` (4 bytes)
- `prev_page_id_`: `page_id_t` (4 bytes)
- `array_`: Array of `LeafEntry` slots:
  - `key`: `int64_t` (8 bytes Little-Endian)
  - `rid`: `RID` (`page_id_t` 4 B + `slot_id_t` 2 B + 2 B padding = 8 bytes)

Effective leaf slot capacity: $\lfloor(4096 - 36) / 16\rfloor = 253$ entries per page.

### Internal Page Layout

Internal pages store keys and child page pointers:

- `array_[0].page_id`: Pointer to child subtree for keys strictly less than `array_[1].key` (`array_[0].key` is dummy/unused).
- `array_[i] (i >= 1)`: Separator `key` ($8$ B) and child `page_id` ($4$ B).

Effective internal slot capacity: $\lfloor(4096 - 28) / 12\rfloor \approx 339$ child pointers per page.

---

## 2. Core Operations

### 2.1 Point Lookup ($O(\log N)$)
1. Starting from `root_page_id_`, recursively fetch internal pages.
2. In each internal node, binary search finds child pointer $P_i$ such that $K_i \le \text{key} < K_{i+1}$.
3. Pin next child page, unpin current page.
4. In the target leaf page, binary search finds the exact key and returns its `RID`.

### 2.2 Insertion & Node Splitting
1. Traverse down to the leaf page corresponding to the key.
2. Insert `(key, RID)` in sorted order. If key exists, update in-place.
3. **Split Condition**: When `leaf->GetSize() > leaf->GetMaxSize()`:
   - Allocate new leaf page.
   - Half of the entries ($N / 2$) are transferred to the new right sibling.
   - Sibling pointers (`prev_page_id`, `next_page_id`) are linked atomically.
   - The smallest key in the right leaf (`new_leaf->KeyAt(0)`) is promoted to the parent via `InsertIntoParent()`.
4. If the parent exceeds capacity, the internal node splits:
   - Middle separator key is promoted to the grandparent.
   - Left internal node keeps $P_0 \dots P_{mid-1}$.
   - Right internal node gets $P_{mid} \dots P_N$.
   - Child pages have their `parent_page_id` updated.
5. If the root overflows, a new root is created, increasing tree height by 1.

### 2.3 Deletion, Coalescing & Redistribution
1. Remove key and shift elements left.
2. If size drops below minimum degree $\lfloor(M+1)/2\rfloor$:
   - Check adjacent sibling in parent.
   - **Coalesce (Merge)**: If `size + sibling_size <= max_size`, merge right node into left node, delete empty page in buffer pool, and delete separator key in parent.
   - **Redistribute (Borrow)**: Otherwise, borrow an entry from the sibling and update the parent separator key.
3. If root has only 1 child after internal coalescence, the child becomes the new root, decreasing tree height by 1.

### 2.4 Range Scans & Iterators
The `IndexIterator` provides standard C++ iterator semantics (`++`, `*`, `!=`):
- `Begin()`: Fetches leftmost leaf page at index 0.
- `Begin(key)`: Binary searches starting leaf page.
- `operator++()`: Advances within leaf; if slot reaches size, fetches `next_page_id` with buffer pool pin/unpin transitions.
