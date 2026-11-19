# CeeBase Python Driver

Official Python DB-API 2.0 compliant driver for CeeBase.

## Installation

```bash
pip install -e bindings/python
```

## Quickstart

```python
import ceebase

with ceebase.connect("app.cbdb") as conn:
    cur = conn.cursor()
    cur.execute("CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT, balance DOUBLE);")
    
    # Parameterized query with typed bindings
    cur.execute("INSERT INTO users VALUES (?, ?, ?);", (1, "Alice Walker", 250.75))
    
    # Typed fetch
    cur.execute("SELECT id, name, balance FROM users WHERE id = ?;", (1,))
    row = cur.fetchone()
    print(row) # (1, 'Alice Walker', 250.75) - true native int, str, float types!
```
