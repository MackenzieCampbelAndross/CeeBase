"""
CeeBase Platform Python SDK
Provides fluent client for Database REST API, Auth, Object Storage,
Functions, and Realtime event subscriptions.
"""

import json
import urllib.parse
import urllib.request
from typing import Any, Dict, List, Optional, Tuple, Union


class QueryBuilder:
    def __init__(self, client: "CeeBaseClient", table: str):
        self.client = client
        self.table = table
        self.method = "GET"
        self.body: Any = None
        self.params: Dict[str, str] = {}

    def select(self, columns: str = "*") -> "QueryBuilder":
        self.method = "GET"
        self.params["select"] = columns
        return self

    def eq(self, column: str, value: Any) -> "QueryBuilder":
        self.params[column] = f"eq.{value}"
        return self

    def neq(self, column: str, value: Any) -> "QueryBuilder":
        self.params[column] = f"neq.{value}"
        return self

    def gt(self, column: str, value: Any) -> "QueryBuilder":
        self.params[column] = f"gt.{value}"
        return self

    def lt(self, column: str, value: Any) -> "QueryBuilder":
        self.params[column] = f"lt.{value}"
        return self

    def like(self, column: str, pattern: str) -> "QueryBuilder":
        self.params[column] = f"like.{pattern}"
        return self

    def order(self, column: str, ascending: bool = True) -> "QueryBuilder":
        self.params["order"] = f"{column}.{'asc' if ascending else 'desc'}"
        return self

    def limit(self, count: int) -> "QueryBuilder":
        self.params["limit"] = str(count)
        return self

    def offset(self, count: int) -> "QueryBuilder":
        self.params["offset"] = str(count)
        return self

    def insert(self, values: Union[Dict[str, Any], List[Dict[str, Any]]]) -> "QueryBuilder":
        self.method = "POST"
        self.body = values
        return self

    def update(self, values: Dict[str, Any]) -> "QueryBuilder":
        self.method = "PATCH"
        self.body = values
        return self

    def delete(self) -> "QueryBuilder":
        self.method = "DELETE"
        return self

    def execute(self) -> Tuple[Optional[Any], Optional[str]]:
        qs = urllib.parse.urlencode(self.params)
        url = f"{self.client.url}/rest/v1/{self.table}"
        if qs:
            url += f"?{qs}"

        token = self.client.auth.current_token or self.client.key
        headers = {
            "Content-Type": "application/json",
            "apikey": self.client.key,
            "Authorization": f"Bearer {token}"
        }

        data_bytes = None
        if self.body is not None and self.method in ("POST", "PATCH"):
            data_bytes = json.dumps(self.body).encode("utf-8")

        req = urllib.request.Request(url, data=data_bytes, headers=headers, method=self.method)
        try:
            with urllib.request.urlopen(req) as resp:
                raw = resp.read().decode("utf-8")
                return json.loads(raw) if raw else None, None
        except urllib.error.HTTPError as e:
            err_body = e.read().decode("utf-8")
            try:
                err_json = json.loads(err_body)
                return None, err_json.get("error", err_body)
            except Exception:
                return None, err_body
        except Exception as e:
            return None, str(e)


class AuthClient:
    def __init__(self, client: "CeeBaseClient"):
        self.client = client
        self.current_session: Optional[Dict[str, Any]] = None
        self.current_token: Optional[str] = None
        self.current_user: Optional[Dict[str, Any]] = None

    def sign_up(self, email: str, password: str, metadata: Optional[Dict[str, Any]] = None) -> Tuple[Optional[Dict[str, Any]], Optional[str]]:
        url = f"{self.client.url}/auth/v1/signup"
        payload = {"email": email, "password": password, "metadata": metadata or {}}
        data, err = self._post_json(url, payload)
        if not err and data and "session" in data:
            self.current_session = data["session"]
            self.current_token = data["session"].get("access_token")
            self.current_user = data.get("user")
        return data, err

    def sign_in_with_password(self, email: str, password: str) -> Tuple[Optional[Dict[str, Any]], Optional[str]]:
        url = f"{self.client.url}/auth/v1/token?grant_type=password"
        payload = {"email": email, "password": password}
        data, err = self._post_json(url, payload)
        if not err and data:
            self.current_session = data
            self.current_token = data.get("access_token")
            self.current_user = data.get("user")
        return data, err

    def sign_in_with_otp(self, email: str) -> Tuple[Optional[Dict[str, Any]], Optional[str]]:
        url = f"{self.client.url}/auth/v1/otp"
        return self._post_json(url, {"email": email})

    def verify_otp(self, email: str, code: str) -> Tuple[Optional[Dict[str, Any]], Optional[str]]:
        url = f"{self.client.url}/auth/v1/verify"
        data, err = self._post_json(url, {"email": email, "code": code})
        if not err and data:
            self.current_session = data
            self.current_token = data.get("access_token")
            self.current_user = data.get("user")
        return data, err

    def sign_out(self) -> Tuple[bool, Optional[str]]:
        url = f"{self.client.url}/auth/v1/logout"
        token = self.current_token or ""
        req = urllib.request.Request(url, data=b"{}", headers={
            "apikey": self.client.key,
            "Authorization": f"Bearer {token}",
            "Content-Type": "application/json"
        }, method="POST")
        try:
            with urllib.request.urlopen(req) as resp:
                self.current_session = None
                self.current_token = None
                self.current_user = None
                return True, None
        except Exception as e:
            return False, str(e)

    def _post_json(self, url: str, payload: Dict[str, Any]) -> Tuple[Optional[Dict[str, Any]], Optional[str]]:
        req = urllib.request.Request(
            url,
            data=json.dumps(payload).encode("utf-8"),
            headers={"Content-Type": "application/json", "apikey": self.client.key},
            method="POST"
        )
        try:
            with urllib.request.urlopen(req) as resp:
                raw = resp.read().decode("utf-8")
                return json.loads(raw), None
        except urllib.error.HTTPError as e:
            err_body = e.read().decode("utf-8")
            try:
                err_json = json.loads(err_body)
                return None, err_json.get("error", err_body)
            except Exception:
                return None, err_body
        except Exception as e:
            return None, str(e)


class StorageBucketClient:
    def __init__(self, client: "CeeBaseClient", bucket_id: str):
        self.client = client
        self.bucket_id = bucket_id

    def upload(self, path: str, data: bytes, content_type: str = "application/octet-stream") -> Tuple[Optional[Dict[str, Any]], Optional[str]]:
        clean_path = path.lstrip("/")
        url = f"{self.client.url}/storage/v1/object/{self.bucket_id}/{clean_path}"
        token = self.client.auth.current_token or self.client.key
        req = urllib.request.Request(url, data=data, headers={
            "Content-Type": content_type,
            "apikey": self.client.key,
            "Authorization": f"Bearer {token}"
        }, method="POST")
        try:
            with urllib.request.urlopen(req) as resp:
                return json.loads(resp.read().decode("utf-8")), None
        except Exception as e:
            return None, str(e)

    def download(self, path: str) -> Tuple[Optional[bytes], Optional[str]]:
        clean_path = path.lstrip("/")
        url = f"{self.client.url}/storage/v1/object/{self.bucket_id}/{clean_path}"
        token = self.client.auth.current_token or self.client.key
        req = urllib.request.Request(url, headers={
            "apikey": self.client.key,
            "Authorization": f"Bearer {token}"
        })
        try:
            with urllib.request.urlopen(req) as resp:
                return resp.read(), None
        except Exception as e:
            return None, str(e)

    def create_signed_url(self, path: str, expires_in: int = 3600) -> Tuple[Optional[str], Optional[str]]:
        clean_path = path.lstrip("/")
        url = f"{self.client.url}/storage/v1/object/sign/{self.bucket_id}/{clean_path}"
        payload = json.dumps({"expires_in": expires_in}).encode("utf-8")
        req = urllib.request.Request(url, data=payload, headers={
            "Content-Type": "application/json",
            "apikey": self.client.key
        }, method="POST")
        try:
            with urllib.request.urlopen(req) as resp:
                data = json.loads(resp.read().decode("utf-8"))
                return f"{self.client.url}{data['signed_url']}", None
        except Exception as e:
            return None, str(e)


class StorageClient:
    def __init__(self, client: "CeeBaseClient"):
        self.client = client

    def from_(self, bucket_id: str) -> StorageBucketClient:
        return StorageBucketClient(self.client, bucket_id)


class FunctionsClient:
    def __init__(self, client: "CeeBaseClient"):
        self.client = client

    def invoke(self, name: str, payload: Optional[Dict[str, Any]] = None) -> Tuple[Optional[Dict[str, Any]], Optional[str]]:
        url = f"{self.client.url}/functions/v1/{name}/invoke"
        token = self.client.auth.current_token or self.client.key
        body = json.dumps(payload or {}).encode("utf-8")
        req = urllib.request.Request(url, data=body, headers={
            "Content-Type": "application/json",
            "apikey": self.client.key,
            "Authorization": f"Bearer {token}"
        }, method="POST")
        try:
            with urllib.request.urlopen(req) as resp:
                res = json.loads(resp.read().decode("utf-8"))
                return res.get("data", res), None
        except Exception as e:
            return None, str(e)


class CeeBaseClient:
    def __init__(self, url: str, key: str):
        self.url = url.rstrip("/")
        self.key = key
        self.auth = AuthClient(self)
        self.storage = StorageClient(self)
        self.functions = FunctionsClient(self)

    def table(self, name: str) -> QueryBuilder:
        return QueryBuilder(self, name)

    def from_(self, name: str) -> QueryBuilder:
        return QueryBuilder(self, name)


def create_client(url: str, key: str) -> CeeBaseClient:
    return CeeBaseClient(url, key)
