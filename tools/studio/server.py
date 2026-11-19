#!/usr/bin/env python3
"""
CeeBase Studio: Visual Web Management Dashboard & Versioned REST API Server
Provides versioned /api/v1/ REST management API and web dashboard.
"""

import http.server
import json
import os
import shutil
import socketserver
import struct
import sys
import time
import urllib.parse
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
        self.start_time = time.time()
        self.query_count = 0
        self.conn = ceebase.connect(self.db_path)
        self._ensure_sample_data()

    def _ensure_sample_data(self):
        res = self.run_query("SHOW TABLES;")
        if not res["rows"]:
            self.run_query("CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT, balance DOUBLE);")
            self.run_query("INSERT INTO users VALUES (1, 'Alice Walker', 250.75);")
            self.run_query("INSERT INTO users VALUES (2, 'Bob Chen', 85.00);")
            self.run_query("INSERT INTO users VALUES (3, 'Charlie Davis', 510.20);")

    def run_query(self, sql: str, params=None):
        self.query_count += 1
        start = time.perf_counter()
        try:
            cur = self.conn.execute(sql, params)
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

    def get_tables(self):
        res = self.run_query("SHOW TABLES;")
        tables = []
        if res["success"]:
            for row in res["rows"]:
                tbl_name = row[0]
                count_res = self.run_query(f"SELECT COUNT(*) FROM {tbl_name};")
                cnt = count_res["rows"][0][0] if count_res["success"] and count_res["rows"] else 0

                sample_res = self.run_query(f"SELECT * FROM {tbl_name} LIMIT 1;")
                cols = sample_res["columns"] if sample_res["success"] else []

                tables.append({
                    "name": tbl_name,
                    "row_count": cnt,
                    "columns": cols
                })
        return tables

    def get_page_diagnostics(self):
        path = Path(self.db_path)
        if not path.exists():
            return []

        pages = []
        size = path.stat().st_size
        num_pages = size // 4096

        with open(path, "rb") as f:
            for pid in range(num_pages):
                raw = f.read(4096)
                if len(raw) < 4096:
                    break

                if pid == 0:
                    magic, v_maj, v_min, p_size, total_p, free_p, cat_p, crc = struct.unpack("<IHHI IIII", raw[0:32])
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
    sessions = {"mock_session_admin": {"user": "admin@ceebase.local", "role": "PLATFORM_ADMIN"}}
    backups = []

    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path

        if path in ("/", "/index.html"):
            self.serve_file(self.static_dir / "index.html", "text/html")
        elif path in ("/health", "/api/v1/health"):
            uptime = round(time.time() - self.backend.start_time, 2)
            self.send_json({
                "status": "healthy",
                "version": ceebase.__version__,
                "storage": "online",
                "uptime_seconds": uptime
            })
        elif path in ("/metrics", "/api/v1/metrics"):
            self.send_json({
                "query_count": self.backend.query_count,
                "uptime_seconds": round(time.time() - self.backend.start_time, 2),
                "active_connections": 1,
                "buffer_pool_hit_ratio": 0.985
            })
        elif path in ("/api/tables", "/api/v1/databases/default/tables"):
            data = self.backend.get_tables()
            self.send_json(data)
        elif path in ("/api/pages", "/api/v1/databases/default/pages"):
            data = self.backend.get_page_diagnostics()
            self.send_json(data)
        elif path == "/api/v1/databases":
            self.send_json([
                {"name": "default", "status": "ONLINE", "tables": len(self.backend.get_tables())}
            ])
        elif path == "/api/v1/users":
            self.send_json([
                {"id": "user_root_admin", "email": "admin@ceebase.local", "role": "PLATFORM_ADMIN", "status": "ACTIVE"}
            ])
        elif path == "/api/v1/orgs":
            self.send_json([
                {"id": "org_platform", "name": "Platform Root", "slug": "system", "status": "ACTIVE"}
            ])
        elif path == "/api/v1/backups":
            self.send_json(self.backups)
        elif path.startswith("/api/table/"):
            table_name = path.replace("/api/table/", "")
            res = self.backend.run_query(f"SELECT * FROM {table_name};")
            self.send_json(res)
        else:
            super().do_GET()

    def do_POST(self):
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path
        length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(length).decode("utf-8") if length > 0 else "{}"
        try:
            payload = json.loads(body)
        except Exception:
            payload = {}

        if path in ("/api/query", "/api/v1/query"):
            sql = payload.get("sql", "")
            params = payload.get("parameters", None)
            res = self.backend.run_query(sql, params)
            self.send_json(res)
        elif path == "/api/v1/auth/login":
            email = payload.get("email", "")
            password = payload.get("password", "")
            if email == "admin@ceebase.local" and password == "CeeBaseAdmin2026!Secure":
                token = "cbs_live_session_token_12345"
                self.sessions[token] = {"user": email, "role": "PLATFORM_ADMIN"}
                self.send_json({"success": True, "token": token, "role": "PLATFORM_ADMIN"})
            else:
                self.send_json({"success": False, "error": "Invalid credentials"}, status=401)
        elif path == "/api/v1/backups":
            backup_name = payload.get("name", f"backup_{int(time.time())}.cbdb")
            backup_path = Path("backups") / backup_name
            backup_path.parent.mkdir(exist_ok=True)
            shutil.copy2(self.backend.db_path, backup_path)
            record = {"name": backup_name, "path": str(backup_path), "created_at": time.time()}
            self.backups.append(record)
            self.send_json({"success": True, "backup": record})
        else:
            self.send_error(404, "Endpoint not found")

    def send_json(self, data, status=200):
        blob = json.dumps(data).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(blob)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Headers", "Content-Type, Authorization")
        self.end_headers()
        self.wfile.write(blob)

    def do_OPTIONS(self):
        self.send_response(204)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type, Authorization")
        self.end_headers()

    def serve_file(self, filepath: Path, content_type: str):
        if not filepath.exists():
            self.send_error(404, "File Not Found")
            return
        with open(filepath, "rb") as f:
            content = f.read()
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(content)))
        self.end_headers()
        self.wfile.write(content)


def run_server(port=DEFAULT_PORT, db_path=DB_FILENAME):
    backend = StudioBackend(db_path)
    StudioHandler.backend = backend

    class ThreadedHTTPServer(socketserver.ThreadingMixIn, http.server.HTTPServer):
        daemon_threads = True

    server = ThreadedHTTPServer(("127.0.0.1", port), StudioHandler)
    print(f"CeeBase Studio REST API & Visual Console running at http://localhost:{port}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down CeeBase Studio Server...")
        server.server_close()


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_PORT
    run_server(port)
