"""
CeeBase Python Client Usage Example
"""

import sys
from pathlib import Path

# Add bindings/python to sys.path
sys.path.insert(0, str(Path(__file__).resolve().parent))

import ceebase


def main():
    db_file = "python_demo.cbdb"

    # Connect to embedded database
    print(f"[INFO] Connecting to CeeBase: {db_file}")
    with ceebase.connect(db_file) as conn:
        # Create table
        conn.execute("CREATE TABLE products (id INTEGER PRIMARY KEY, name TEXT, price DOUBLE);")
        print("[INFO] Table 'products' created.")

        # Insert records
        conn.execute("INSERT INTO products VALUES (1, 'Mechanical Keyboard', 89.99);")
        conn.execute("INSERT INTO products VALUES (2, 'Wireless Mouse', 49.50);")
        conn.execute("INSERT INTO products VALUES (3, '4K Monitor', 320.00);")
        print("[INFO] Inserted 3 products.")

        # Query records
        cur = conn.execute("SELECT id, name, price FROM products ORDER BY id;")
        print("\nQuery Results:")
        print("-" * 50)
        for row in cur.fetchall():
            print(f"ID: {row[0]} | Name: {row[1]:<22} | Price: ${row[2]}")
        print("-" * 50)

    # Clean up demo database
    for p in [Path(db_file), Path(db_file).with_suffix(".wal")]:
        if p.exists():
            p.unlink()
    print("[SUCCESS] CeeBase Python client workflow completed successfully!")


if __name__ == "__main__":
    main()
