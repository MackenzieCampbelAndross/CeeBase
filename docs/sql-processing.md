# SQL Processing Infrastructure: Lexer, Parser & Semantic Analysis

## Overview

**CeeBase** implements a complete SQL compilation pipeline from first principles, free of third-party parsing tools (such as Flex, Bison, ANTLR, or SQLite's lemon).

```
SQL Query String
       │
       ▼ [Lexer]
Token Stream
       │
       ▼ [Recursive Descent Parser]
Abstract Syntax Tree (AST)
       │
       ▼ [Semantic Analyzer / Catalog Validation]
Validated Bound AST
       │
       ▼ [Planner & Optimizer]
Physical Execution Plan
```

---

## 1. SQL Lexer (`ceebase::Lexer`)

The lexer scans raw input strings and produces a linear token vector. It handles line and column tracking for syntax error diagnostics.

### Supported Token Types

1. **Keywords**:
   `SELECT`, `FROM`, `WHERE`, `INSERT`, `INTO`, `VALUES`, `UPDATE`, `SET`, `DELETE`, `CREATE`, `TABLE`, `DROP`, `INDEX`, `ON`, `ORDER`, `BY`, `ASC`, `DESC`, `LIMIT`, `AND`, `OR`, `NOT`, `JOIN`, `INNER`, `GROUP`, `HAVING`, `COUNT`, `SUM`, `AVG`, `MIN`, `MAX`, `EXPLAIN`, `PRIMARY`, `KEY`, `TRUE`, `FALSE`, `NULL`
2. **Data Type Keywords**:
   `INTEGER`, `INT`, `BIGINT`, `FLOAT`, `DOUBLE`, `BOOLEAN`, `BOOL`, `TEXT`, `VARCHAR`
3. **Literals**:
   - Integers: `12345`
   - Floating-point: `3.14159`, `.5`
   - String literals: `'hello world'` (supporting escape sequences `\'`)
   - Booleans: `TRUE`, `FALSE`
   - Null: `NULL`
4. **Identifiers & Qualified Names**:
   `table_name`, `column_name`, `table.column`
5. **Operators & Punctuation**:
   `=`, `!=`, `<>`, `<`, `<=`, `>`, `>=`, `+`, `-`, `*`, `/`, `.`, `,`, `;`, `(`, `)`

---

## 2. SQL Parser & Abstract Syntax Tree (`ceebase::Parser`)

The parser is implemented as a deterministic recursive descent parser using Pratt precedence climbing for arithmetic and logical expressions.

### Expression Precedence Hierarchy (Highest to Lowest)

1. **Primary Expressions**: Literals, Column references (`col`, `tbl.col`), Aggregate functions (`COUNT(*)`, `SUM(val)`), Parenthesized expressions.
2. **Multiplicative Operators**: `*`, `/`
3. **Additive Operators**: `+`, `-`
4. **Relational Operators**: `=`, `!=`, `<>`, `<`, `<=`, `>`, `>=`
5. **Logical NOT**: `NOT`
6. **Logical AND**: `AND`
7. **Logical OR**: `OR`

### Supported Statement AST Nodes

- `CreateTableStatement`: Table name, typed column definitions, primary key flags, nullability constraints.
- `DropTableStatement`: Target table name.
- `CreateIndexStatement`: Index name, target table, indexed key column.
- `InsertStatement`: Target table, optional column list, multi-row value expressions (`VALUES (...), (...)`).
- `SelectStatement`: Projection list (columns, aggregates, expressions), FROM table, optional INNER JOIN with ON predicate, WHERE filter, GROUP BY list, HAVING predicate, ORDER BY with ASC/DESC, LIMIT count.
- `UpdateStatement`: Target table, column-expression assignment pairs, optional WHERE clause.
- `DeleteStatement`: Target table, optional WHERE filter.
- `ExplainStatement`: Wraps any statement to output logical plan, physical plan, and optimizer cost model diagnostics.

---

## 3. Semantic Analysis & Catalog Validation

Before a parsed AST is executed, the `Planner` validates it against catalog metadata:

1. **Table Existence**: Verifies that FROM, JOIN, INSERT, UPDATE, and DELETE target tables exist.
2. **Column Resolution**:
   - Checks that all referenced column identifiers exist in the target schema.
   - Resolves table-qualified column names (`employees.dept_id`).
3. **Type Compatibility**:
   - Validates that values provided in `INSERT` match schema column types.
   - Ensures binary comparison operators are applied to mutually comparable types.
4. **Constraint Checking**:
   - Ensures primary key columns are unique and non-nullable.
