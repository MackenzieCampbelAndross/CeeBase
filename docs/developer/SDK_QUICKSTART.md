# CeeBase Developer Ecosystem & SDK Quickstarts

This guide covers integrating CeeBase into external applications across C, C++, Python, Node.js, Java, and the REST Management API.

---

## 1. Native C API

Header: `include/ceebase/ceebase_c.h`
Shared Library: `libceebase.dll` (Windows) / `libceebase.so` (Linux)

### Complete C Workflow
```c
#include "ceebase/ceebase_c.h"
#include <stdio.h>

int main(void) {
    ceebase_db* db = NULL;
    if (ceebase_open("app.cbdb", &db) != CEEBASE_OK) {
        fprintf(stderr, "Error opening DB\n");
        return 1;
    }

    // 1. DDL
    ceebase_exec(db, "CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT, balance DOUBLE);", NULL);

    // 2. Parameterized Prepared Statement
    ceebase_stmt* stmt = NULL;
    ceebase_prepare(db, "INSERT INTO users VALUES (?, ?, ?);", &stmt);
    ceebase_bind_int(stmt, 1, 101);
    ceebase_bind_text(stmt, 2, "Alice Walker", -1);
    ceebase_bind_double(stmt, 3, 250.75);
    ceebase_step(stmt);
    ceebase_finalize(stmt);

    // 3. Query Execution with Typed Accessors
    ceebase_prepare(db, "SELECT id, name, balance FROM users WHERE id = ?;", &stmt);
    ceebase_bind_int(stmt, 1, 101);
    if (ceebase_step(stmt) == CEEBASE_ROW) {
        int id = ceebase_column_int(stmt, 0);
        const char* name = ceebase_column_text(stmt, 1);
        double balance = ceebase_column_double(stmt, 2);
        printf("User #%d: %s, Balance: $%.2f\n", id, name, balance);
    }
    ceebase_finalize(stmt);

    // 4. Close Connection
    ceebase_close(db);
    return 0;
}
```

---

## 2. Modern C++ API (C++20)

Header: `include/ceebase/ceebase.hpp`

```cpp
#include "ceebase/ceebase.hpp"
#include <iostream>

int main() {
    try {
        ceebase::Connection conn("app.cbdb");

        // DDL & Mutations
        conn.Execute("CREATE TABLE items (id INTEGER PRIMARY KEY, title TEXT, price DOUBLE);");

        // Parameterized PreparedStatement
        auto stmt = conn.Prepare("INSERT INTO items VALUES (?, ?, ?);");
        stmt.Bind(1, 1);
        stmt.Bind(2, "Wireless Headphones");
        stmt.Bind(3, 89.99);
        stmt.Step();

        // Transaction Management
        conn.Begin();
        conn.Execute("INSERT INTO items VALUES (2, 'Charging Case', 29.50);");
        conn.Commit();

        // Typed Query
        auto query = conn.Prepare("SELECT id, title, price FROM items ORDER BY id;");
        while (query.Step()) {
            std::cout << "Item: " << query.GetString(1) << " ($" << query.GetDouble(2) << ")\n";
        }

        conn.Close();
    } catch (const ceebase::Exception& e) {
        std::cerr << "Database error: " << e.what() << "\n";
    }
}
```

---

## 3. Python SDK (DB-API 2.0 Compliant)

Package: `pip install -e bindings/python`

```python
import ceebase

with ceebase.connect("ecommerce.cbdb") as conn:
    cur = conn.cursor()
    cur.execute("CREATE TABLE inventory (sku INTEGER PRIMARY KEY, name TEXT, price DOUBLE);")

    # Parameterized insert
    cur.execute("INSERT INTO inventory VALUES (?, ?, ?);", (101, "Mechanical Keyboard", 129.99))

    # Query with native types (no string conversion!)
    cur.execute("SELECT sku, name, price FROM inventory WHERE sku = ?;", (101,))
    sku, name, price = cur.fetchone()
    print(f"SKU {sku}: {name} - ${price:.2f}")

    # Transaction handling with context manager (auto-commit on exit, auto-rollback on exception)
```

---

## 4. Node.js & TypeScript SDK

Package: `@ceebase/client` (`bindings/nodejs`)

```typescript
import { connect, ConnectionPool } from '@ceebase/client';

async function main() {
    const conn = connect('services.cbdb');

    // Create table & execute parameterized statements
    await conn.execute('CREATE TABLE microservices (id INTEGER PRIMARY KEY, name TEXT, port INTEGER, active BOOLEAN);');
    await conn.execute('INSERT INTO microservices VALUES (?, ?, ?, ?);', [1, 'Auth Gateway', 8080, true]);

    // Typed Query Result
    const res = await conn.query('SELECT * FROM microservices WHERE id = ?;', [1]);
    console.table(res.rows); // Types preserved: id: number, name: string, active: boolean

    // ACID Transactions
    await conn.transaction(async (tx) => {
        await tx.execute('INSERT INTO microservices VALUES (?, ?, ?, ?);', [2, 'Billing Service', 8081, true]);
    });

    conn.close();

    // Connection Pooling
    const pool = new ConnectionPool({ database: 'services.cbdb', maxConnections: 5 });
    await pool.withConnection(async (c) => {
        const q = await c.query('SELECT COUNT(*) FROM microservices;');
        console.log('Count:', q.rows);
    });
    pool.closeAll();
}

main().catch(console.error);
```

---

## 5. Java SDK (Java 22+ Panama FFM)

Class: `com.ceebase.Ceebase` (`bindings/java`)

```java
import com.ceebase.Ceebase;

public class App {
    public static void main(String[] args) {
        try (Ceebase.Connection conn = Ceebase.connect("bank.cbdb")) {
            conn.execute("CREATE TABLE accounts (id INTEGER PRIMARY KEY, holder TEXT, balance DOUBLE);");

            try (Ceebase.PreparedStatement stmt = conn.prepareStatement("INSERT INTO accounts VALUES (?, ?, ?);")) {
                stmt.setInt(1, 1001);
                stmt.setString(2, "Alice Walker");
                stmt.setDouble(3, 1500.50);
                stmt.executeUpdate();
            }

            try (Ceebase.PreparedStatement stmt = conn.prepareStatement("SELECT id, holder, balance FROM accounts WHERE id = ?;")) {
                stmt.setInt(1, 1001);
                try (Ceebase.ResultSet rs = stmt.executeQuery()) {
                    if (rs.next()) {
                        System.out.printf("Account: %s | Balance: $%.2f%n", rs.getString(1), rs.getDouble(2));
                    }
                }
            }
        }
    }
}
```

---

## 6. Versioned REST Management API (`/api/v1/`)

Port: `8080` (CeeBase Studio / REST Server: `python tools/studio/server.py`)

| Method | Endpoint | Description |
| :--- | :--- | :--- |
| `GET` | `/api/v1/health` | Service health status and uptime |
| `GET` | `/api/v1/metrics` | Engine query throughput and buffer pool cache hit ratio |
| `POST` | `/api/v1/auth/login` | Authenticate with email and password |
| `POST` | `/api/v1/query` | Execute SQL statement with parameter bindings |
| `GET` | `/api/v1/databases` | List accessible tenant databases |
| `GET` | `/api/v1/databases/{name}/tables` | Inspect schemas, columns and row counts |
| `POST` | `/api/v1/backups` | Take online hot snapshot backup |
| `GET` | `/api/v1/backups` | List historical backups |

### REST Query Example:
```bash
curl -X POST http://localhost:8080/api/v1/query \
  -H "Content-Type: application/json" \
  -d '{"sql": "SELECT id, name, balance FROM users WHERE id = ?;", "parameters": [1]}'
```
