"""
Comprehensive Automated Test Suite for CeeBase Platform Services:
JWT, Auth Service, Row-Level Security (RLS), Object Storage,
Realtime Subscriptions, Functions Runtime, and Gateway REST APIs.
"""

import json
import shutil
import sys
import tempfile
import time
import unittest
import urllib.parse
import urllib.request
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(REPO_ROOT))
sys.path.insert(0, str(REPO_ROOT / "bindings" / "python"))

from src.platform.auth_service import AuthService
from src.platform.functions_runtime import FunctionsRuntime
from src.platform.gateway import GatewayHandler, PlatformManager
from src.platform.jwt_util import create_project_tokens, decode_jwt, encode_jwt
from src.platform.realtime_service import RealtimeService
from src.platform.rls_engine import RLSEngine, RLSPolicy
from src.platform.storage_service import StorageService


class TestCeeBasePlatform(unittest.TestCase):
    def setUp(self):
        self.temp_dir = Path(tempfile.mkdtemp(prefix="ceebase_platform_test_"))
        self.jwt_secret = "test-secret-key-32-chars-long-abc"

    def tearDown(self):
        shutil.rmtree(self.temp_dir, ignore_errors=True)

    # --- 1. JWT Utilities ---

    def test_jwt_generation_and_verification(self):
        tokens = create_project_tokens("test_proj", self.jwt_secret)
        self.assertIn("anon_key", tokens)
        self.assertIn("service_role_key", tokens)

        # Verify anon key
        valid, payload, err = decode_jwt(tokens["anon_key"], self.jwt_secret)
        self.assertTrue(valid, err)
        self.assertEqual(payload["role"], "anon")
        self.assertEqual(payload["sub"], "test_proj")

        # Verify service role key
        valid, payload, err = decode_jwt(tokens["service_role_key"], self.jwt_secret)
        self.assertTrue(valid, err)
        self.assertEqual(payload["role"], "service_role")

        # Tampered token fails
        tampered = tokens["anon_key"][:-4] + "abcd"
        valid, _, err = decode_jwt(tampered, self.jwt_secret)
        self.assertFalse(valid)

    # --- 2. Auth Service ---

    def test_auth_lifecycle(self):
        auth = AuthService(self.temp_dir / "auth", self.jwt_secret)

        # Sign Up
        res = auth.sign_up("developer@example.com", "Password123!", {"name": "Alice"})
        self.assertEqual(res["user"]["email"], "developer@example.com")
        self.assertIn("access_token", res["session"])
        user_id = res["user"]["id"]

        # Duplicate email fails
        with self.assertRaises(ValueError):
            auth.sign_up("developer@example.com", "AnotherPassword")

        # Sign in with correct password
        login_res = auth.sign_in_with_password("developer@example.com", "Password123!")
        self.assertEqual(login_res["user"]["id"], user_id)
        refresh_token = login_res["session"]["refresh_token"]

        # Sign in with wrong password fails
        with self.assertRaises(PermissionError):
            auth.sign_in_with_password("developer@example.com", "WrongPassword")

        # Refresh token rotation
        refreshed = auth.refresh_session(refresh_token)
        self.assertIn("access_token", refreshed["session"])
        new_refresh = refreshed["session"]["refresh_token"]
        self.assertNotEqual(refresh_token, new_refresh)

        # Old refresh token is now revoked
        with self.assertRaises(PermissionError):
            auth.refresh_session(refresh_token)

        # OTP Lifecycle
        otp = auth.generate_otp("otp_user@example.com")
        self.assertEqual(len(otp), 6)
        otp_res = auth.verify_otp("otp_user@example.com", otp)
        self.assertEqual(otp_res["user"]["email"], "otp_user@example.com")

        # Logout
        self.assertTrue(auth.sign_out(new_refresh))

    # --- 3. Row-Level Security (RLS) ---

    def test_rls_policy_enforcement(self):
        rls = RLSEngine(self.temp_dir / "rls")
        table = "documents"
        rls.enable_rls(table)
        self.assertTrue(rls.is_rls_enabled(table))

        # Define policy: users can only select rows where user_id = auth.uid()
        policy = RLSPolicy(
            name="users_read_own_docs",
            table=table,
            operation="SELECT",
            roles=["authenticated"],
            using_expr="user_id = auth.uid()"
        )
        rls.add_policy(policy)

        # Anonymous user cannot read (no policy allows anon)
        anon_ctx = {"role": "anon", "id": "anon"}
        allowed, where, _ = rls.authorize_and_filter_query(table, "SELECT", anon_ctx)
        self.assertFalse(allowed)

        # Authenticated user 'usr_100' gets rewritten WHERE clause
        auth_ctx = {"role": "authenticated", "id": "usr_100", "email": "alice@site.com"}
        allowed, where, matched = rls.authorize_and_filter_query(table, "SELECT", auth_ctx)
        self.assertTrue(allowed)
        self.assertEqual(where, "(user_id = 'usr_100')")

        # Service role bypasses RLS completely
        service_ctx = {"role": "service_role"}
        allowed, where, _ = rls.authorize_and_filter_query(table, "SELECT", service_ctx, "status = 'active'")
        self.assertTrue(allowed)
        self.assertEqual(where, "status = 'active'")

    # --- 4. Object Storage Service ---

    def test_storage_operations(self):
        storage = StorageService(self.temp_dir / "storage", self.jwt_secret, quota_bytes=10 * 1024 * 1024)

        # Create bucket
        bucket = storage.create_bucket("avatars", public=False)
        self.assertEqual(bucket.name, "avatars")
        self.assertFalse(bucket.public)

        # Upload file
        data = b"Hello, CeeBase Storage Kernel!"
        meta = storage.upload_object("avatars", "users/profile.txt", data, content_type="text/plain")
        self.assertEqual(meta["size"], len(data))

        # Download file
        retrieved, mime, _ = storage.get_object("avatars", "users/profile.txt")
        self.assertEqual(retrieved, data)
        self.assertEqual(mime, "text/plain")

        # List files
        files = storage.list_objects("avatars")
        self.assertEqual(len(files), 1)
        self.assertEqual(files[0]["name"], "users/profile.txt")

        # Signed URL
        token = storage.create_signed_url("avatars", "users/profile.txt", expires_in_seconds=60)
        self.assertTrue(storage.verify_signed_url("avatars", "users/profile.txt", token))
        self.assertFalse(storage.verify_signed_url("avatars", "other/file.txt", token))

        # Delete file
        self.assertTrue(storage.delete_object("avatars", "users/profile.txt"))
        self.assertEqual(len(storage.list_objects("avatars")), 0)

    # --- 5. Functions Runtime ---

    def test_functions_execution(self):
        functions = FunctionsRuntime(self.temp_dir / "functions")
        proj_id = "proj_test"

        # Deploy function
        fn_code = """
def handler(event):
    name = event.get("name", "World")
    return {"message": f"Hello, {name} from CeeBase Functions!"}
"""
        fn = functions.deploy_function(proj_id, "hello", fn_code)
        self.assertEqual(fn.name, "hello")

        # Invoke function
        res = functions.invoke(proj_id, "hello", {"name": "Developer"})
        self.assertTrue(res["success"], res.get("error"))
        self.assertEqual(res["data"]["message"], "Hello, Developer from CeeBase Functions!")

        # Check logs
        logs = functions.get_logs(proj_id, "hello")
        self.assertGreaterEqual(len(logs), 1)
        self.assertEqual(logs[0]["function"], "hello")

    # --- 6. Realtime Service ---

    def test_realtime_changefeed(self):
        realtime = RealtimeService()

        # Subscribe to 'orders' table
        sub = realtime.subscribe("proj_1", "channel_orders", "orders", event_filter="INSERT")

        # Publish INSERT on 'orders'
        delivered = realtime.publish_event("proj_1", "orders", "INSERT", {"id": 1, "item": "Laptop"})
        self.assertEqual(delivered, 1)

        # Subscriber receives event
        event = sub.queue.get_nowait()
        self.assertEqual(event["type"], "INSERT")
        self.assertEqual(event["record"]["item"], "Laptop")

        # Publish UPDATE on 'orders' (subscriber only listens to INSERT)
        delivered = realtime.publish_event("proj_1", "orders", "UPDATE", {"id": 1, "item": "Desktop"})
        self.assertEqual(delivered, 0)
        self.assertTrue(sub.queue.empty())

        realtime.unsubscribe(sub.id)

    # --- 7. Full Gateway HTTP E2E Integration ---

    def test_gateway_http_e2e(self):
        import http.server
        import socketserver
        import threading

        platform = PlatformManager(str(self.temp_dir / "platform"))
        GatewayHandler.platform = platform

        class TestServer(socketserver.ThreadingMixIn, http.server.HTTPServer):
            daemon_threads = True

        port = 8991
        server = TestServer(("127.0.0.1", port), GatewayHandler)
        t = threading.Thread(target=server.serve_forever, daemon=True)
        t.start()
        time.sleep(0.3)

        base_url = f"http://127.0.0.1:{port}"

        try:
            # 1. Health check
            req = urllib.request.urlopen(f"{base_url}/api/v1/health")
            data = json.loads(req.read().decode())
            self.assertEqual(data["status"], "healthy")

            # 2. Project creation
            create_proj_data = json.dumps({"name": "App Backend", "organization_id": "org_e2e"}).encode()
            req = urllib.request.Request(
                f"{base_url}/api/v1/projects",
                data=create_proj_data,
                headers={"Content-Type": "application/json"}
            )
            res = json.loads(urllib.request.urlopen(req).read().decode())
            self.assertEqual(res["status"], "READY")
            proj_id = res["id"]
            anon_key = res["anon_key"]
            service_role_key = res["service_role_key"]

            # 3. User Sign Up
            signup_data = json.dumps({"email": "e2e@site.com", "password": "SecretPassword123!"}).encode()
            req = urllib.request.Request(
                f"{base_url}/auth/v1/signup",
                data=signup_data,
                headers={"Content-Type": "application/json", "X-Project-Id": proj_id, "apikey": anon_key}
            )
            res = json.loads(urllib.request.urlopen(req).read().decode())
            self.assertIn("access_token", res["session"])
            access_token = res["session"]["access_token"]

            # 4. Storage Bucket & File Upload
            proj = platform.get_or_load_project(proj_id)
            proj.storage.create_bucket("media", public=True)

            file_content = b"E2E Test Media Binary Data"
            req = urllib.request.Request(
                f"{base_url}/storage/v1/object/media/test.bin",
                data=file_content,
                headers={"Content-Type": "application/octet-stream", "X-Project-Id": proj_id, "Authorization": f"Bearer {access_token}"}
            )
            res = json.loads(urllib.request.urlopen(req).read().decode())
            self.assertEqual(res["name"], "test.bin")

            # Download public file
            dl_req = urllib.request.urlopen(f"{base_url}/storage/v1/object/media/test.bin")
            self.assertEqual(dl_req.read(), file_content)

            # 5. Functions Deploy & Invoke
            fn_data = json.dumps({
                "name": "calculate",
                "code": "def handler(e):\n    return {'result': e.get('a', 0) * e.get('b', 0)}"
            }).encode()
            req = urllib.request.Request(
                f"{base_url}/functions/v1",
                data=fn_data,
                headers={"Content-Type": "application/json", "X-Project-Id": proj_id, "Authorization": f"Bearer {service_role_key}"}
            )
            res = json.loads(urllib.request.urlopen(req).read().decode())
            self.assertEqual(res["name"], "calculate")

            # Invoke Function
            inv_data = json.dumps({"a": 7, "b": 6}).encode()
            req = urllib.request.Request(
                f"{base_url}/functions/v1/calculate/invoke",
                data=inv_data,
                headers={"Content-Type": "application/json", "X-Project-Id": proj_id}
            )
            res = json.loads(urllib.request.urlopen(req).read().decode())
            self.assertTrue(res["success"])
            self.assertEqual(res["data"]["result"], 42)

        finally:
            server.shutdown()
            server.server_close()


if __name__ == "__main__":
    unittest.main()
