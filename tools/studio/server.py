#!/usr/bin/env python3
"""
CeeBase Studio: Visual Web Management Dashboard
Zero-dependency web interface built on Python standard library http.server.
"""

import http.server
import json
import os
import socketserver
import struct
import sys
import time
import urllib.parse
import webbrowser
from pathlib import Path

# Add bindings/python to sys.path
REPO_ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(REPO_ROOT / "bindings" / "python"))

import ceebase

DEFAULT_PORT = 8080
DB_FILENAME = "ceebase.cbdb"


class StudioBackend:
    def __init__(self, db_path: str):
        self.db_path = db_path
        self.conn = ceebase.connect(self.db_path)
        self._ensure_sample_data()

    def _ensure_sample_data(self):
        res = self.run_query("SHOW TABLES;")
        if not res["rows"]:
            self.run_query("CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT, balance DOUBLE);")
            self.run_query("INSERT INTO users VALUES (1, 'Alice Walker', 250.75);")
            self.run_query("INSERT INTO users VALUES (2, 'Bob Chen', 85.00);")
            self.run_query("INSERT INTO users VALUES (3, 'Charlie Davis', 510.20);")

    def run_query(self, sql: str):
        start = time.perf_counter()
        try:
            cur = self.conn.execute(sql)
            cols = [c[0] for c in cur.description] if cur.description else []
            rows = cur.fetchall()
            elapsed_ms = (time.perf_counter() - start) * 1000.0
            return {
                "success": True,
                "columns": cols,
                "rows": rows,
                "rowcount": cur.rowcount,
                "elapsed_ms": round(elapsed_ms, 3),
                "error": None
            }
        except Exception as e:
            elapsed_ms = (time.perf_counter() - start) * 1000.0
            return {
                "success": False,
                "columns": [],
                "rows": [],
                "rowcount": 0,
                "elapsed_ms": round(elapsed_ms, 3),
                "error": str(e)
            }
            return {
                "success": False,
                "columns": [],
                "rows": [],
                "rowcount": 0,
                "elapsed_ms": round(elapsed_ms, 3),
                "error": str(e)
            }

    def get_tables(self):
        # Scan tables and fetch count & columns
        res = self.run_query("SHOW TABLES;")
        tables = []
        if res["success"]:
            for row in res["rows"]:
                tbl_name = row[0]
                # Get count and schema
                count_res = self.run_query(f"SELECT COUNT(*) FROM {tbl_name};")
                cnt = count_res["rows"][0][0] if count_res["success"] and count_res["rows"] else "0"
                
                # Fetch 1 row for columns
                sample_res = self.run_query(f"SELECT * FROM {tbl_name} LIMIT 1;")
                cols = sample_res["columns"] if sample_res["success"] else []

                tables.append({
                    "name": tbl_name,
                    "row_count": cnt,
                    "columns": cols
                })
        return tables

    def get_page_diagnostics(self):
        # Inspect raw 4KB pages in .cbdb file
        path = Path(self.db_path)
        if not path.exists():
            return []

        file_size = path.stat().st_size
        total_pages = file_size // 4096
        pages = []

        with open(path, "rb") as f:
            for pid in range(min(total_pages, 50)):  # Max 50 pages preview
                raw = f.read(4096)
                if len(raw) < 4096:
                    break

                # Inspect page
                if pid == 0:
                    magic, v_maj, v_min, page_size, total_p, free_p, cat_p = struct.unpack("<IHHIIII", raw[0:24])
                    crc = struct.unpack("<I", raw[4092:4096])[0]
                    pages.append({
                        "id": pid,
                        "type": "HEADER_PAGE",
                        "details": f"Magic: 0x{magic:08X} (CBDB), Version: {v_maj}.{v_min}, Pages: {total_p}, CRC32: 0x{crc:08X}",
                        "allocated": 4096,
                        "free_space": 0
                    })
                elif pid == 1:
                    pages.append({
                        "id": pid,
                        "type": "CATALOG_PAGE",
                        "details": "System Catalog metadata, schemas and index definitions",
                        "allocated": 4096,
                        "free_space": 0
                    })
                else:
                    # Slotted page header inspection
                    # PageID (4B), LSN (8B), FreeSpacePtr (2B), SlotCount (2B)
                    page_id, lsn, free_ptr, slot_count = struct.unpack("<IQHH", raw[0:16])
                    crc = struct.unpack("<I", raw[4092:4096])[0]
                    free_space = free_ptr - (16 + slot_count * 4) if free_ptr >= (16 + slot_count * 4) else 0
                    pages.append({
                        "id": pid,
                        "type": "DATA_SLOTTED_PAGE",
                        "details": f"Slot Count: {slot_count} tuples, FreeSpacePtr: {free_ptr}, CRC32: 0x{crc:08X}",
                        "allocated": 4096 - free_space,
                        "free_space": max(0, free_space)
                    })

        return pages


class StudioHandler(http.server.SimpleHTTPRequestHandler):
    backend = None
    static_dir = Path(__file__).resolve().parent / "static"

    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path

        if path == "/" or path == "/index.html":
            self.serve_file(self.static_dir / "index.html", "text/html")
        elif path == "/api/tables":
            data = self.backend.get_tables()
            self.send_json(data)
        elif path == "/api/pages":
            data = self.backend.get_page_diagnostics()
            self.send_json(data)
        elif path.startswith("/api/table/"):
            table_name = path.replace("/api/table/", "")
            res = self.backend.run_query(f"SELECT * FROM {table_name};")
            self.send_json(res)
        else:
            # Fallback static files
            super().do_GET()

    def do_POST(self):
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path

        if path == "/api/query":
            length = int(self.headers.get("Content-Length", 0))
            body = self.rfile.read(length).decode("utf-8")
            payload = json.loads(body)
            sql = payload.get("sql", "")
            res = self.backend.run_query(sql)
            self.send_json(res)
        else:
            self.send_error(404, "Endpoint not found")

    def send_json(self, data):
        blob = json.dumps(data).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(blob)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(blob)

    def serve_file(self, filepath: Path, content_type: str):
        if not filepath.exists():
            self.send_error(404, "File Not Found")
            return
        blob = filepath.read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", f"{content_type}; charset=utf-8")
        self.send_header("Content-Length", str(len(blob)))
        self.end_headers()
        self.wfile.write(blob)

    def log_message(self, format, *args):
        # Silent clean logging
        pass


def main():
    db_file = sys.argv[1] if len(sys.argv) > 1 else DB_FILENAME
    backend = StudioBackend(db_file)
    StudioHandler.backend = backend

    port = DEFAULT_PORT
    for attempt in range(5):
        try:
            with socketserver.TCPServer(("", port), StudioHandler) as httpd:
                url = f"http://localhost:{port}"
                print("========================================================")
                print("  CeeBase Studio: Visual Web Management Dashboard")
                print("========================================================")
                print(f" [ONLINE] Database: {db_file}")
                print(f" [ONLINE] Dashboard URL: {url}")
                print(" Press Ctrl+C in this terminal to stop the server.")
                print("========================================================\n")
                
                # Auto-open browser
                try:
                    webbrowser.open(url)
                except Exception:
                    pass
                
                httpd.serve_forever()
                break
        except OSError as e:
            if "Address already in use" in str(e) or e.errno == 10048:
                port += 1
            else:
                raise


if __name__ == "__main__":
    main()
