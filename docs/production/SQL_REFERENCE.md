# CeeBase SQL Dialect & Syntax Reference

CeeBase implements a clean, ANSI-compliant relational SQL dialect optimized for high-performance transactions and indexing.

---

## 1. Supported Data Types

| Type | Description | Storage Representation | Example |
| :--- | :--- | :--- | :--- |
| `INT` / `INTEGER` | 32-bit signed integer | 4 bytes, little-endian | `42` |
| `BIGINT` | 64-bit signed integer | 8 bytes, little-endian | `9223372036854775807` |
| `FLOAT` | 32-bit IEEE 754 float | 4 bytes | `3.1415` |
| `DOUBLE` | 64-bit IEEE 754 double | 8 bytes | `199.99` |
| `BOOLEAN` | Boolean true or false | 1 byte (`0` or `1`) | `true`, `false` |
| `VARCHAR` / `TEXT` | Variable-length UTF-8 string | 2-byte length prefix + bytes | `'Alice Smith'` |
| `NULL` | Absence of value | Null bitmap representation | `NULL` |

---

## 2. Data Definition Language (DDL)

### `CREATE TABLE`
Creates a new relation with typed columns and primary key constraints.
```sql
CREATE TABLE employees (
    id INT PRIMARY KEY,
    name VARCHAR,
    department VARCHAR,
    salary DOUBLE,
    active BOOLEAN
);
```

### `DROP TABLE`
Deletes a table, its catalog registration, slotted pages, and associated B+ Tree indexes.
```sql
DROP TABLE employees;
```

---

## 3. Data Manipulation Language (DML)

### `INSERT INTO`
Inserts a tuple into the table heap and updates all registered indexes.
```sql
INSERT INTO employees VALUES (101, 'Alex Mercer', 'Infrastructure', 115000.0, true);
INSERT INTO employees VALUES (102, 'Elena Rostova', 'Kernel Storage', 135000.0, true);
```

### `SELECT`
Retrieves rows from a relation, with optional filtering, projections, and limit clauses.
```sql
-- Full scan with projections
SELECT id, name, salary FROM employees;

-- Point lookup utilizing B+ Tree index
SELECT * FROM employees WHERE id = 101;

-- Aggregations
SELECT COUNT(*) FROM employees;

-- Limit
SELECT * FROM employees LIMIT 10;
```

### `UPDATE`
Updates matching rows in-place or marks old tuple deleted and inserts updated record.
```sql
UPDATE employees SET salary = 125000.0 WHERE id = 101;
```

### `DELETE`
Deletes matching rows from the slotted page and removes index entries.
```sql
DELETE FROM employees WHERE id = 102;
```

---

## 4. Transaction Control Commands

```sql
-- Begin a new ACID transaction
BEGIN;

-- Insert or update records
INSERT INTO employees VALUES (103, 'David Kim', 'Security', 120000.0, true);

-- Persist changes to WAL and commit
COMMIT;

-- Or abort and roll back uncommitted changes
ROLLBACK;
```

---

## 5. Inspection & Diagnostic Commands

### `SHOW TABLES`
Lists all user tables registered in the persistent catalog.
```sql
SHOW TABLES;
```

### `EXPLAIN`
Displays the logical plan, execution cost estimate, and physical index scan access path.
```sql
EXPLAIN SELECT * FROM employees WHERE id = 101;
```
