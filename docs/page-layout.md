# CeeBase Page Layout Specification

## 1. Page Header (General Layout)

Every data page in CeeBase has a standard header formatted using explicit Little-Endian binary encoding:

| Offset (Bytes) | Field | Type | Description |
|:---|:---|:---|:---|
| 0 - 3 | `page_type` | `uint32_t` | `1` = Slotted Data Page, `2` = B+ Tree Internal, `3` = B+ Tree Leaf, `4` = Free Page |
| 4 - 7 | `page_id` | `uint32_t` | Self-identifying page identifier |
| 8 - 15 | `page_lsn` | `uint64_t` | Log Sequence Number of last modifying WAL record |
| 16 - 19 | `prev_page_id` | `uint32_t` | Linked list pointer for table heap or index leaf |
| 20 - 23 | `next_page_id` | `uint32_t` | Linked list pointer for table heap or index leaf |
| 24 - 27 | `slot_count` | `uint16_t` | Number of slot entries in the page slot directory |
| 28 - 29 | `free_space_pointer` | `uint16_t` | Byte offset pointing to bottom of free space |
| 30 - 4091 | Payload / Slots | Variable | Slotted entries growing downward, data growing upward |
| 4092 - 4095 | `checksum` | `uint32_t` | IEEE 802.3 CRC-32 checksum of page bytes 0 to 4091 |

## 2. Slotted Page Mechanics

Slotted pages prevent external fragmentation when variable-length records (such as `TEXT` columns) are stored and updated.

```
+---------------------------------------------------------------+
|                      Standard Page Header                     |
+---------------------------------------------------------------+
| Slot 0 [Offset, Len] | Slot 1 [Offset, Len] | ...             |
| ---> (Slot directory grows forward)                           |
+---------------------------------------------------------------+
|                                                               |
|                        FREE SPACE REGION                      |
|                                                               |
+---------------------------------------------------------------+
| <--- (Record tuples grow backward from byte 4091)             |
| ... | Record 1 Payload       | Record 0 Payload               |
+---------------------------------------------------------------+
|                     CRC-32 Checksum (4B)                      |
+---------------------------------------------------------------+
```

- **Slot Directory**:
  - Each entry is 4 bytes: `offset` (`uint16_t`) and `length` (`uint16_t`).
  - A deleted slot marks `offset = 0` or a sentinel value.
- **Record Identifier (RID)**:
  - Formed as `(page_id, slot_id)`.
  - Allows records to be relocated or compacted within the page without invalidating external references or index pointers.
