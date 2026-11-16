# NexusDB Storage Engine Specification

## 1. Subsystem Scope

The storage engine subsystem manages physical database files on persistent storage. It translates abstract database page requests (`page_id_t`) to fixed physical byte offsets in the file:

$$\text{File Offset} = \text{page\_id} \times \text{PAGE\_SIZE}$$

where $\text{PAGE\_SIZE} = 4096\text{ bytes}$.

## 2. File Layout & Header Page (Page 0)

Every NexusDB database file consists of contiguous 4096-byte pages.
Page 0 is reserved as the **Database Header Page**:

| Byte Offset | Field Name | Data Type | Description |
|:---|:---|:---|:---|
| 0 - 3 | Magic Bytes | `uint32_t` | `0x4E584442` ("NXDB") |
| 4 - 7 | Version Major / Minor | `uint16_t` $\times 2$ | Format schema compatibility |
| 8 - 11 | Page Size | `uint32_t` | Enforced at 4096 bytes |
| 12 - 15 | Total Allocated Pages | `uint32_t` | High water mark of pages in file |
| 16 - 19 | First Free Page ID | `uint32_t` | Head of free-page linked list |
| 20 - 23 | Catalog Root Page ID | `uint32_t` | Root page for system catalog tables |
| 24 - 4091 | Reserved / Bitmap | Bytes | Free page bitmap or reserved expansion |
| 4092 - 4095 | Checksum | `uint32_t` | CRC-32 covering bytes 0 through 4091 |

## 3. Physical I/O Model

- Reads and writes operate strictly on complete 4096-byte aligned blocks.
- Synchronous I/O routines with explicit `fsync` / OS flushing support for durability.
- Corruption detection via CRC-32 checksum validation prior to page consumption.
