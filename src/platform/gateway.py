"""
CeeBase Unified Platform API Gateway & Data Plane
Routes incoming client requests (CLI, Studio, Desktop, SDKs) to:
Control Plane, Database REST API, Auth Service, Object Storage,
Realtime Subscriptions, Functions Runtime, and Observability.
"""

import http.server
import json
import os
import re
import socketserver
import struct
import sys
import threading
import time
import urllib.parse
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(REPO_ROOT))
sys.path.insert(0, str(REPO_ROOT / "bindings" / "python"))

try:
    import ceebase
except ImportError:
    ceebase = None

from src.cloud.control_plane import CloudControlPlane, DeploymentStatus
from src.infra.backup_manager import BackupManager
from src.infra.metrics import GLOBAL_METRICS, GLOBAL_LOGGER
from src.infra.replication import ReplicationNode, ReplicationRole
from src.infra.storage_maintenance import StorageMaintenanceManager
from src.platform.auth_service import AuthService
from src.platform.functions_runtime import FunctionsRuntime
from src.platform.jwt_util import create_project_tokens, decode_jwt, encode_jwt
from src.platform.realtime_service import RealtimeService
from src.platform.rls_engine import RLSEngine, RLSPolicy
from src.platform.storage_service import StorageService


class ProjectInstance:
    """Encapsulates all runtime services for a single provisioned project."""

    def __init__(self, project_id: str, db_path: Path, data_dir: Path, jwt_secret: str):
        self.project_id = project_id
        self.db_path = Path(db_path)
        self.data_dir = Path(data_dir)
        self.data_dir.mkdir(parents=True, exist_ok=True)
        self.jwt_secret = jwt_secret

        # Initialize embedded database connection
        self.conn = None
        if ceebase is not None:
            try:
                self.conn = ceebase.connect(str(self.db_path))
            except Exception as e:
                GLOBAL_LOGGER.error(f"Failed to open database for project {project_id}: {e}")

        # Core project services
        self.auth = AuthService(self.data_dir / "auth", self.jwt_secret)
        self.rls = RLSEngine(self.data_dir / "rls")
        self.storage = StorageService(self.data_dir / "storage", self.jwt_secret)
        self.functions = FunctionsRuntime(self.data_dir / "functions")
        self.tokens = create_project_tokens(project_id, self.jwt_secret)

    def run_query(self, sql: str, params: Optional[list] = None) -> Dict[str, Any]:
        """Execute query safely against project's CeeBase database."""
        if not self.conn:
            return {"success": False, "error": "Database connection not available"}

        start = time.perf_counter()
        try:
            cur = self.conn.execute(sql, params)
            cols = [c[0] for c in cur.description] if cur.description else []
            rows = cur.fetchall() if cur.description else []
            elapsed_ms = (time.perf_counter() - start) * 1000.0
            GLOBAL_METRICS.record_query(elapsed_ms / 1000.0, tenant=self.project_id)
            return {
                "success": True,
                "columns": cols,
                "rows": rows,
                "row_count": len(rows),
                "elapsed_ms": round(elapsed_ms, 2)
            }
        except Exception as e:
            elapsed_ms = (time.perf_counter() - start) * 1000.0
            return {
                "success": False,
                "error": str(e),
                "elapsed_ms": round(elapsed_ms, 2)
            }


class PlatformManager:
    """Manages project lifecycles, project routing, and shared platform resources."""

    def __init__(self, base_dir: str = "platform_data"):
        self.base_dir = Path(base_dir)
        self.base_dir.mkdir(parents=True, exist_ok=True)
        self.control_plane = CloudControlPlane(str(self.base_dir / "control_plane"))
        self.realtime = RealtimeService()
        self.backup_mgr = BackupManager(str(self.base_dir / "backups"))
        self.projects: Dict[str, ProjectInstance] = {}
        self.lock = threading.Lock()

        # Bootstrap default project if needed
        self._ensure_default_project()

    def _ensure_default_project(self):
        default_id = "default"
        if default_id not in self.projects:
            db_file = REPO_ROOT / "ceebase.cbdb"
            data_dir = self.base_dir / "projects" / default_id
            self.projects[default_id] = ProjectInstance(
                project_id=default_id,
                db_path=db_file,
                data_dir=data_dir,
                jwt_secret="ceebase-default-secret-production-key-32chars"
            )

    def get_or_load_project(self, project_id: str) -> Optional[ProjectInstance]:
        with self.lock:
            if project_id in self.projects:
                return self.projects[project_id]

            # Check if project exists in control plane
            dep = self.control_plane.deployments.get(project_id)
            if not dep:
                return None

            data_dir = self.base_dir / "projects" / project_id
            db_path = Path(dep.get("db_path", self.base_dir / f"{project_id}.cbdb"))
            secret = dep.get("jwt_secret", f"secret-for-{project_id}-32-chars-long")

            instance = ProjectInstance(project_id, db_path, data_dir, secret)
            self.projects[project_id] = instance
            return instance

    def create_project(self, org_id: str, name: str, region: str = "us-east") -> Dict[str, Any]:
        """Provisions a new project in the control plane."""
        project_id = f"proj_{secrets_token(8)}"
        db_path = self.base_dir / f"{project_id}.cbdb"
        secret = f"ceebase_secret_{secrets_token(16)}"

        # Ensure organization exists
        if org_id not in self.control_plane.organizations:
            slug = org_id[4:] if org_id.startswith("org_") else org_id
            created_org = self.control_plane.create_organization(
                name=slug.replace("_", " ").title(),
                slug=slug,
                billing_email=f"admin@{slug}.com",
                plan="free"
            )
            org_id = created_org["org_id"]

        # Step 1: Provision in control plane
        dep = self.control_plane.provision_deployment(
            org_id=org_id,
            deployment_name=name,
            model="SHARED_MULTI_TENANT"
        )
        dep["id"] = project_id
        dep["jwt_secret"] = secret
        dep["db_path"] = str(db_path)
        dep["region"] = region
        dep["created_at"] = time.time()
        self.control_plane.deployments[project_id] = dep
        self.control_plane._save_state()

        # Step 2: Initialize project instance
        data_dir = self.base_dir / "projects" / project_id
        instance = ProjectInstance(project_id, db_path, data_dir, secret)
        with self.lock:
            self.projects[project_id] = instance

        return {
            "id": project_id,
            "name": name,
            "organization_id": org_id,
            "status": "READY",
            "region": region,
            "api_url": f"/api/v1/projects/{project_id}",
            "anon_key": instance.tokens["anon_key"],
            "service_role_key": instance.tokens["service_role_key"],
            "created_at": dep["created_at"]
        }


GATEWAY_START_TIME = time.time()

def secrets_token(n: int = 8) -> str:
    import secrets
    return secrets.token_hex(n)


class GatewayHandler(http.server.SimpleHTTPRequestHandler):
    """Unified HTTP handler for all platform REST APIs, Auth, Storage, and Realtime."""

    platform: PlatformManager = None  # Injected before server starts

    def do_OPTIONS(self):
        """Handle CORS pre-flight requests."""
        self.send_response(200)
        self._send_cors_headers()
        self.end_headers()

    def _send_cors_headers(self):
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, PUT, PATCH, DELETE, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type, Authorization, apikey, X-Project-Id, X-Client-Info")

    def _send_json(self, data: Any, status: int = 200):
        body = json.dumps(data, indent=2).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self._send_cors_headers()
        self.end_headers()
        self.wfile.write(body)

    def _send_error(self, message: str, status: int = 400):
        self._send_json({"error": message, "status": status}, status=status)

    def _resolve_project_and_context(self) -> Tuple[Optional[ProjectInstance], Dict[str, Any]]:
        """
        Resolves the target project and the authenticated user context
        from Headers (X-Project-Id, apikey, Authorization: Bearer <jwt>).
        """
        proj_id = self.headers.get("X-Project-Id", "default")
        auth_header = self.headers.get("Authorization", "")
        apikey = self.headers.get("apikey", "")

        token = ""
        if auth_header.startswith("Bearer "):
            token = auth_header[7:].strip()
        elif apikey:
            token = apikey.strip()

        project = self.platform.get_or_load_project(proj_id)
        if not project:
            # Fallback to default
            project = self.platform.get_or_load_project("default")

        ctx = {"role": "anon", "id": "anon"}
        if token and project:
            valid, payload, _ = decode_jwt(token, project.jwt_secret, verify_exp=False)
            if valid and payload:
                ctx = {
                    "role": payload.get("role", "anon"),
                    "id": payload.get("sub", "anon"),
                    "email": payload.get("email"),
                    "jwt": payload
                }
        return project, ctx

    # --- HTTP Dispatchers ---

    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path
        query = urllib.parse.parse_qs(parsed.query)

        # 1. Observability: Health & Metrics
        if path == "/api/v1/health":
            self._send_json({
                "status": "healthy",
                "version": "1.0.0",
                "uptime_sec": round(time.time() - GATEWAY_START_TIME, 2),
                "engine": "CeeBase C++20 Storage Kernel",
                "active_projects": len(self.platform.projects)
            })
            return
        elif path == "/api/v1/metrics":
            prom_text = GLOBAL_METRICS.to_prometheus_text().encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/plain; version=0.0.4")
            self.send_header("Content-Length", str(len(prom_text)))
            self._send_cors_headers()
            self.end_headers()
            self.wfile.write(prom_text)
            return

        # 2. Control Plane: Projects & Orgs
        elif path == "/api/v1/projects":
            proj_list = []
            for pid, p in self.platform.projects.items():
                proj_list.append({
                    "id": pid,
                    "db_path": str(p.db_path),
                    "anon_key": p.tokens["anon_key"],
                    "service_role_key": p.tokens["service_role_key"]
                })
            self._send_json(proj_list)
            return
        elif path == "/api/v1/organizations":
            self._send_json(list(self.platform.control_plane.organizations.values()))
            return

        # 3. Auth Service: /auth/v1/user & /auth/v1/admin/users
        elif path == "/auth/v1/user":
            project, ctx = self._resolve_project_and_context()
            if not project or ctx.get("role") == "anon":
                self._send_error("Unauthorized", 401)
                return
            self._send_json(ctx)
            return
        elif path == "/auth/v1/admin/users":
            project, ctx = self._resolve_project_and_context()
            if not project or ctx.get("role") != "service_role":
                self._send_error("Forbidden: service_role required", 403)
                return
            self._send_json(project.auth.list_users())
            return

        # 4. Storage Service: /storage/v1/bucket & /storage/v1/object/list
        elif path == "/storage/v1/bucket":
            project, _ = self._resolve_project_and_context()
            self._send_json(project.storage.list_buckets() if project else [])
            return
        elif path.startswith("/storage/v1/object/list/"):
            bucket_id = path[len("/storage/v1/object/list/"):].strip("/")
            project, _ = self._resolve_project_and_context()
            if not project:
                self._send_error("Project not found", 404)
                return
            prefix = query.get("prefix", [""])[0]
            try:
                objs = project.storage.list_objects(bucket_id, prefix=prefix)
                self._send_json(objs)
            except Exception as e:
                self._send_error(str(e), 400)
            return
        elif path.startswith("/storage/v1/object/"):
            # Download file
            parts = path[len("/storage/v1/object/"):].split("/", 1)
            if len(parts) == 2:
                bucket_id, file_path = parts
                project, ctx = self._resolve_project_and_context()
                token = query.get("token", [""])[0]

                # Verify bucket public or signed token or authenticated
                bucket = project.storage.get_bucket(bucket_id) if project else None
                if not bucket:
                    for p in self.platform.projects.values():
                        b = p.storage.get_bucket(bucket_id)
                        if b:
                            bucket = b
                            project = p
                            break

                is_authorized = False
                if bucket and bucket.public:
                    is_authorized = True
                elif token and project.storage.verify_signed_url(bucket_id, file_path, token):
                    is_authorized = True
                elif ctx.get("role") in ("authenticated", "service_role"):
                    is_authorized = True

                if not is_authorized:
                    self._send_error("Unauthorized: private object requires signed URL or credentials", 401)
                    return

                try:
                    data, mime, _ = project.storage.get_object(bucket_id, file_path)
                    self.send_response(200)
                    self.send_header("Content-Type", mime)
                    self.send_header("Content-Length", str(len(data)))
                    self._send_cors_headers()
                    self.end_headers()
                    self.wfile.write(data)
                    return
                except Exception as e:
                    self._send_error(str(e), 404)
                    return

        # 5. Functions Service: /functions/v1
        elif path == "/functions/v1":
            project, _ = self._resolve_project_and_context()
            self._send_json(project.functions.list_functions(project.project_id) if project else [])
            return

        # 6. Realtime Service: /realtime/v1/metrics
        elif path == "/realtime/v1/metrics":
            self._send_json(self.platform.realtime.get_metrics())
            return

        # 7. Database REST API: /rest/v1/:table (SELECT)
        elif path.startswith("/rest/v1/"):
            table_name = path[len("/rest/v1/"):].strip("/")
            if table_name == "openapi.json":
                # Schema introspection and OpenAPI 3.0 export
                project, _ = self._resolve_project_and_context()
                self._send_json(self._generate_openapi_spec(project))
                return

            project, ctx = self._resolve_project_and_context()
            if not project:
                self._send_error("Project not found", 404)
                return

            # Parse query params (select, order, limit, offset, and column filters)
            select_cols = query.get("select", ["*"])[0]
            limit_val = query.get("limit", [None])[0]
            offset_val = query.get("offset", [None])[0]
            order_val = query.get("order", [None])[0]

            where_clauses = []
            for k, vals in query.items():
                if k in ("select", "limit", "offset", "order"):
                    continue
                v = vals[0]
                if v.startswith("eq."):
                    val_str = v[3:]
                    if val_str.isdigit() or (val_str.startswith("-") and val_str[1:].isdigit()):
                        where_clauses.append(f"{k} = {val_str}")
                    else:
                        where_clauses.append(f"{k} = '{val_str}'")
                elif v.startswith("gt."):
                    where_clauses.append(f"{k} > {v[3:]}")
                elif v.startswith("lt."):
                    where_clauses.append(f"{k} < {v[3:]}")
                elif v.startswith("like."):
                    where_clauses.append(f"{k} LIKE '{v[5:]}'")

            raw_where = " AND ".join(where_clauses) if where_clauses else None

            # RLS Authorization Check
            allowed, effective_where, _ = project.rls.authorize_and_filter_query(
                table_name, "SELECT", ctx, raw_where
            )
            if not allowed:
                self._send_error("Forbidden by Row-Level Security policy", 403)
                return

            sql = f"SELECT {select_cols} FROM {table_name}"
            if effective_where:
                sql += f" WHERE {effective_where}"
            if order_val:
                sql += f" ORDER BY {order_val.replace('.', ' ')}"
            if limit_val:
                sql += f" LIMIT {int(limit_val)}"
                if offset_val:
                    sql += f" OFFSET {int(offset_val)}"

            res = project.run_query(sql)
            if not res["success"]:
                self._send_error(res.get("error", "Query execution failed"), 400)
                return

            # Format rows as array of JSON dicts
            cols = res["columns"]
            dict_rows = [dict(zip(cols, row)) for row in res["rows"]]
            self._send_json(dict_rows)
            return

        # 8. Studio Web UI Fallback
        static_dir = REPO_ROOT / "tools" / "studio" / "static"
        requested_file = static_dir / (path.lstrip("/") or "index.html")
        if requested_file.is_file():
            self._serve_static_file(requested_file)
            return

        self._send_error("Endpoint not found", 404)

    def do_POST(self):
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path
        query = urllib.parse.parse_qs(parsed.query)

        content_len = int(self.headers.get("Content-Length", 0))
        body_bytes = self.rfile.read(content_len) if content_len > 0 else b""
        payload = {}
        if body_bytes and self.headers.get("Content-Type", "").startswith("application/json"):
            try:
                payload = json.loads(body_bytes.decode("utf-8"))
            except Exception:
                pass

        # 1. Projects: POST /api/v1/projects
        if path == "/api/v1/projects":
            org_id = payload.get("organization_id", "default_org")
            name = payload.get("name", "New Project")
            res = self.platform.create_project(org_id, name)
            self._send_json(res, 201)
            return

        # 2. Auth: /auth/v1/signup, /auth/v1/token, /auth/v1/otp, /auth/v1/verify, /auth/v1/logout
        elif path == "/auth/v1/signup":
            project, _ = self._resolve_project_and_context()
            email = payload.get("email")
            password = payload.get("password")
            if not email or not password:
                self._send_error("Email and password required", 400)
                return
            try:
                res = project.auth.sign_up(email, password, payload.get("metadata"))
                self._send_json(res, 201)
            except Exception as e:
                self._send_error(str(e), 400)
            return

        elif path == "/auth/v1/token":
            project, _ = self._resolve_project_and_context()
            grant_type = query.get("grant_type", [payload.get("grant_type", "password")])[0]
            try:
                if grant_type == "password":
                    res = project.auth.sign_in_with_password(payload["email"], payload["password"])
                    self._send_json(res["session"])
                elif grant_type == "refresh_token":
                    res = project.auth.refresh_session(payload["refresh_token"])
                    self._send_json(res["session"])
                else:
                    self._send_error(f"Unsupported grant_type: {grant_type}", 400)
            except Exception as e:
                self._send_error(str(e), 401)
            return

        elif path == "/auth/v1/otp":
            project, _ = self._resolve_project_and_context()
            email = payload.get("email")
            if not email:
                self._send_error("Email required for OTP", 400)
                return
            code = project.auth.generate_otp(email)
            self._send_json({"message": "OTP code sent", "code": code})
            return

        elif path == "/auth/v1/verify":
            project, _ = self._resolve_project_and_context()
            email = payload.get("email")
            code = payload.get("code")
            try:
                res = project.auth.verify_otp(email, code)
                self._send_json(res["session"])
            except Exception as e:
                self._send_error(str(e), 401)
            return

        elif path == "/auth/v1/logout":
            project, ctx = self._resolve_project_and_context()
            token = self.headers.get("Authorization", "").replace("Bearer ", "").strip()
            success = project.auth.sign_out(token)
            self._send_json({"success": success})
            return

        # 3. Authenticated SQL: /rest/v1/sql or /api/v1/query
        elif path in ("/rest/v1/sql", "/api/v1/query"):
            project, ctx = self._resolve_project_and_context()
            sql = payload.get("query") or payload.get("sql")
            params = payload.get("params")
            if not sql:
                self._send_error("SQL query string required", 400)
                return

            res = project.run_query(sql, params)
            # Dispatch changefeed if query modified tables
            if res["success"]:
                sql_upper = sql.strip().upper()
                if sql_upper.startswith(("INSERT", "UPDATE", "DELETE")):
                    tbl_match = re.search(r"\b(INTO|FROM|UPDATE)\s+([a-zA-Z0-9_]+)", sql, re.IGNORECASE)
                    tbl_name = tbl_match.group(2) if tbl_match else "unknown"
                    op = "INSERT" if sql_upper.startswith("INSERT") else ("UPDATE" if sql_upper.startswith("UPDATE") else "DELETE")
                    self.platform.realtime.publish_event(project.project_id, tbl_name, op)

            self._send_json(res)
            return

        # 4. Database REST API: POST /rest/v1/:table (INSERT)
        elif path.startswith("/rest/v1/"):
            table_name = path[len("/rest/v1/"):].strip("/")
            project, ctx = self._resolve_project_and_context()
            if not project:
                self._send_error("Project not found", 404)
                return

            rows_to_insert = payload if isinstance(payload, list) else [payload]
            inserted_count = 0
            for r in rows_to_insert:
                if not project.rls.authorize_row_data(table_name, "INSERT", ctx, r):
                    self._send_error("Forbidden: row data violates Row-Level Security policy", 403)
                    return

                cols = list(r.keys())
                vals = [f"'{v}'" if isinstance(v, str) else str(v) for v in r.values()]
                sql = f"INSERT INTO {table_name} ({', '.join(cols)}) VALUES ({', '.join(vals)});"
                res = project.run_query(sql)
                if not res["success"]:
                    self._send_error(res.get("error", "Insert failed"), 400)
                    return
                inserted_count += 1

                # Emit committed change event to Realtime
                self.platform.realtime.publish_event(project.project_id, table_name, "INSERT", new_record=r)

            self._send_json({"inserted": inserted_count}, 201)
            return

        # 5. Storage: Buckets, Upload /storage/v1/object/:bucket/* & Signed URLs
        elif path == "/storage/v1/bucket":
            project, _ = self._resolve_project_and_context()
            bucket_id = payload.get("id") or payload.get("name")
            name = payload.get("name", bucket_id)
            public = payload.get("public", False)
            if not bucket_id:
                self._send_error("Bucket id required", 400)
                return
            try:
                b = project.storage.create_bucket(bucket_id, name=name, public=public)
                self._send_json(b.to_dict(), 201)
            except Exception as e:
                self._send_error(str(e), 400)
            return

        elif path.startswith("/storage/v1/object/sign/"):
            parts = path[len("/storage/v1/object/sign/"):].split("/", 1)
            bucket_id, file_path = parts[0], parts[1] if len(parts) > 1 else ""
            project, _ = self._resolve_project_and_context()
            expires = payload.get("expires_in", 3600)
            token = project.storage.create_signed_url(bucket_id, file_path, expires)
            self._send_json({"signed_url": f"/storage/v1/object/{bucket_id}/{file_path}?token={token}"})
            return

        elif path.startswith("/storage/v1/object/"):
            parts = path[len("/storage/v1/object/"):].split("/", 1)
            bucket_id, file_path = parts[0], parts[1] if len(parts) > 1 else ""
            project, ctx = self._resolve_project_and_context()
            content_type = self.headers.get("Content-Type", "application/octet-stream")
            try:
                meta = project.storage.upload_object(bucket_id, file_path, body_bytes, content_type, owner_id=ctx.get("id"))
                self._send_json(meta, 201)
            except Exception as e:
                self._send_error(str(e), 400)
            return

        # 6. Functions: /functions/v1/:name/invoke & Deploy
        elif path == "/functions/v1":
            project, _ = self._resolve_project_and_context()
            name = payload.get("name")
            code = payload.get("code")
            if not name or not code:
                self._send_error("Function name and code required", 400)
                return
            fn = project.functions.deploy_function(project.project_id, name, code)
            self._send_json(fn.to_dict(), 201)
            return

        elif path.startswith("/functions/v1/") and path.endswith("/invoke"):
            name = path[len("/functions/v1/"): -len("/invoke")].strip("/")
            project, _ = self._resolve_project_and_context()
            res = project.functions.invoke(project.project_id, name, payload)
            self._send_json(res, status=res.get("status_code", 200))
            return

        # 7. Realtime: /realtime/v1/broadcast
        elif path == "/realtime/v1/broadcast":
            project, _ = self._resolve_project_and_context()
            tbl = payload.get("table", "*")
            ev_type = payload.get("type", "BROADCAST")
            delivered = self.platform.realtime.publish_event(project.project_id, tbl, ev_type, payload.get("record"))
            self._send_json({"delivered_to": delivered})
            return

        self._send_error("Endpoint not found", 404)

    def _serve_static_file(self, file_path: Path):
        mime, _ = urllib.parse.mimetypes.guess_type(str(file_path))
        with open(file_path, "rb") as f:
            data = f.read()
        self.send_response(200)
        self.send_header("Content-Type", mime or "text/html")
        self.send_header("Content-Length", str(len(data)))
        self._send_cors_headers()
        self.end_headers()
        self.wfile.write(data)

    def _generate_openapi_spec(self, project: ProjectInstance) -> Dict[str, Any]:
        """Dynamically generate OpenAPI 3.0 specification for current database schema."""
        tables_res = project.run_query("SHOW TABLES;")
        tables = [r[0] for r in tables_res.get("rows", [])] if tables_res.get("success") else []

        paths = {}
        for tbl in tables:
            paths[f"/rest/v1/{tbl}"] = {
                "get": {
                    "summary": f"Query rows from {tbl}",
                    "responses": {"200": {"description": "List of rows matching query"}}
                },
                "post": {
                    "summary": f"Insert records into {tbl}",
                    "responses": {"201": {"description": "Records inserted"}}
                }
            }

        return {
            "openapi": "3.0.0",
            "info": {
                "title": f"CeeBase Project API - {project.project_id}",
                "version": "1.0.0",
                "description": "Auto-generated REST API powered by CeeBase C++20 Storage Kernel."
            },
            "paths": paths
        }
