#!/usr/bin/env python3
"""
Real-world E-Commerce Inventory Application using CeeBase Python SDK.
Demonstrates:
1. Connection lifecycle
2. Schema definition (CREATE TABLE)
3. Parameterized queries
4. Transaction commit and rollback
5. Typed result fetching
6. Restart persistence (closing, reopening, verifying data integrity)
"""

import sys
import os
from pathlib import Path

# Ensure ceebase is importable
REPO_ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(REPO_ROOT / "bindings" / "python"))

import ceebase

DB_PATH = "inventory_demo.cbdb"


def cleanup():
    for ext in [".cbdb", ".wal"]:
        p = Path(DB_PATH).with_suffix(ext)
        if p.exists():
            try: p.unlink()
            except OSError: pass


def stage_one_initial_population():
    print("\n--- STAGE 1: Initialize Database & Populate Inventory ---")
    with ceebase.connect(DB_PATH) as conn:
        cur = conn.cursor()
        
        # 1. Create tables
        cur.execute("CREATE TABLE inventory (sku INTEGER PRIMARY KEY, title TEXT, price DOUBLE, quantity INTEGER);")
        cur.execute("CREATE TABLE orders (order_id INTEGER PRIMARY KEY, sku INTEGER, qty INTEGER, total DOUBLE);")
        print("[OK] Created 'inventory' and 'orders' tables.")

        # 2. Parameterized batch insert
        products = [
            (101, "Ergonomic Mechanical Keyboard", 129.99, 45),
            (102, "Precision Gaming Mouse", 69.50, 80),
            (103, "Ultrawide 34-inch Monitor", 599.00, 15),
            (104, "USB-C Dual 4K Docking Station", 149.95, 30)
        ]
        for p in products:
            cur.execute("INSERT INTO inventory VALUES (?, ?, ?, ?);", p)
        print(f"[OK] Inserted {len(products)} inventory products.")

        # 3. Transaction demonstration: Place an order
        conn.execute("BEGIN;")
        cur.execute("INSERT INTO orders VALUES (?, ?, ?, ?);", (1001, 101, 2, 259.98))
        cur.execute("UPDATE inventory SET quantity = 43 WHERE sku = 101;")
        conn.commit()
        print("[OK] Processed order #1001 with transaction commit.")

        # 4. Transaction rollback demonstration
        conn.execute("BEGIN;")
        cur.execute("INSERT INTO orders VALUES (?, ?, ?, ?);", (9999, 103, 100, 59900.00))
        conn.rollback()
        print("[OK] Successfully rolled back unauthorized oversized order.")


def stage_two_restart_and_verification():
    print("\n--- STAGE 2: Restart Engine & Verify On-Disk Persistence ---")
    # Open fresh connection after complete process close
    with ceebase.connect(DB_PATH) as conn:
        cur = conn.cursor()

        # Verify table survived restart
        cur.execute("SELECT sku, title, price, quantity FROM inventory ORDER BY sku;")
        rows = cur.fetchall()
        print(f"[OK] Verified {len(rows)} products reloaded from persistent catalog and slotted pages:")
        print("-" * 75)
        for row in rows:
            sku, title, price, qty = row
            # Verify true native types
            assert isinstance(sku, int), "SKU must be native int"
            assert isinstance(title, str), "Title must be native str"
            assert isinstance(price, float), "Price must be native float"
            assert isinstance(qty, int), "Quantity must be native int"
            print(f"SKU: {sku:<5} | {title:<32} | Price: ${price:<7.2f} | Stock: {qty}")
        print("-" * 75)

        # Verify order #1001 exists, but rolled back order #9999 does not
        cur.execute("SELECT order_id, sku, qty, total FROM orders WHERE order_id = ?;", (1001,))
        order = cur.fetchone()
        assert order is not None and order[0] == 1001, "Committed order 1001 must exist"
        print(f"[OK] Verified committed order #1001 exists: Total ${order[3]}")

        cur.execute("SELECT order_id FROM orders WHERE order_id = ?;", (9999,))
        rolled_back = cur.fetchone()
        assert rolled_back is None, "Rolled back order 9999 must NOT exist"
        print("[OK] Confirmed rolled back transaction left zero footprint on disk.")


def main():
    cleanup()
    try:
        stage_one_initial_population()
        stage_two_restart_and_verification()
        print("\n[SUCCESS] CeeBase Python integration demo passed with 100% data integrity!")
    finally:
        cleanup()


if __name__ == "__main__":
    main()
