"""
CeeBase Platform Object Storage Service
Provides S3-compatible bucket and object management, multipart/streaming uploads,
signed URL generation, content-type verification, and quota enforcement.
"""

import hashlib
import hmac
import json
import mimetypes
import os
import shutil
import time
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

from src.platform.jwt_util import decode_jwt, encode_jwt


class StorageBucket:
    def __init__(
        self,
        id: str,
        name: str,
        public: bool = False,
        file_size_limit_bytes: int = 50 * 1024 * 1024,  # 50 MB default
        allowed_mime_types: Optional[List[str]] = None,
        created_at: Optional[float] = None
    ):
        self.id = id
        self.name = name
        self.public = public
        self.file_size_limit_bytes = file_size_limit_bytes
        self.allowed_mime_types = allowed_mime_types or []
        self.created_at = created_at or time.time()

    def to_dict(self) -> Dict[str, Any]:
        return {
            "id": self.id,
            "name": self.name,
            "public": self.public,
            "file_size_limit": self.file_size_limit_bytes,
            "allowed_mime_types": self.allowed_mime_types,
            "created_at": self.created_at
        }

    @classmethod
    def from_dict(cls, d: Dict[str, Any]) -> "StorageBucket":
        return cls(
            id=d["id"],
            name=d["name"],
            public=d.get("public", False),
            file_size_limit_bytes=d.get("file_size_limit", 50 * 1024 * 1024),
            allowed_mime_types=d.get("allowed_mime_types", []),
            created_at=d.get("created_at")
        )


class StorageService:
    """Manages files and buckets on disk with metadata persistence and signed URLs."""

    def __init__(self, root_dir: Path, signing_secret: str, quota_bytes: int = 1024 * 1024 * 1024):
        self.root_dir = Path(root_dir)
        self.root_dir.mkdir(parents=True, exist_ok=True)
        self.signing_secret = signing_secret
        self.quota_bytes = quota_bytes

        self.meta_dir = self.root_dir / ".meta"
        self.meta_dir.mkdir(parents=True, exist_ok=True)
        self.buckets_file = self.meta_dir / "buckets.json"
        self.objects_file = self.meta_dir / "objects.json"

        self.buckets: Dict[str, StorageBucket] = {}
        self.objects: Dict[str, Dict[str, Any]] = {}
        self._load_state()

    def _load_state(self):
        if self.buckets_file.exists():
            try:
                with open(self.buckets_file, "r", encoding="utf-8") as f:
                    data = json.load(f)
                    self.buckets = {k: StorageBucket.from_dict(v) for k, v in data.items()}
            except Exception:
                self.buckets = {}
        if self.objects_file.exists():
            try:
                with open(self.objects_file, "r", encoding="utf-8") as f:
                    self.objects = json.load(f)
            except Exception:
                self.objects = {}

    def _save_state(self):
        with open(self.buckets_file, "w", encoding="utf-8") as f:
            json.dump({k: v.to_dict() for k, v in self.buckets.items()}, f, indent=2)
        with open(self.objects_file, "w", encoding="utf-8") as f:
            json.dump(self.objects, f, indent=2)

    def get_total_usage_bytes(self) -> int:
        return sum(obj.get("size", 0) for obj in self.objects.values())

    # --- Bucket Operations ---

    def list_buckets(self) -> List[Dict[str, Any]]:
        return [b.to_dict() for b in self.buckets.values()]

    def get_bucket(self, bucket_id: str) -> Optional[StorageBucket]:
        return self.buckets.get(bucket_id)

    def create_bucket(
        self,
        id: str,
        name: Optional[str] = None,
        public: bool = False,
        file_size_limit_bytes: int = 50 * 1024 * 1024,
        allowed_mime_types: Optional[List[str]] = None
    ) -> StorageBucket:
        bid = id.strip().lower()
        if bid in self.buckets:
            raise ValueError(f"Bucket '{bid}' already exists")

        bucket = StorageBucket(
            id=bid,
            name=name or bid,
            public=public,
            file_size_limit_bytes=file_size_limit_bytes,
            allowed_mime_types=allowed_mime_types or []
        )
        self.buckets[bid] = bucket
        bucket_dir = self.root_dir / bid
        bucket_dir.mkdir(parents=True, exist_ok=True)
        self._save_state()
        return bucket

    def delete_bucket(self, bucket_id: str) -> bool:
        bid = bucket_id.strip().lower()
        if bid not in self.buckets:
            raise FileNotFoundError(f"Bucket '{bid}' not found")

        # Delete all objects in bucket
        for key in list(self.objects.keys()):
            if key.startswith(f"{bid}/"):
                del self.objects[key]

        bucket_dir = self.root_dir / bid
        if bucket_dir.exists():
            shutil.rmtree(bucket_dir, ignore_errors=True)

        del self.buckets[bid]
        self._save_state()
        return True

    # --- Object Operations ---

    def upload_object(
        self,
        bucket_id: str,
        path: str,
        data: bytes,
        content_type: Optional[str] = None,
        owner_id: Optional[str] = None
    ) -> Dict[str, Any]:
        bid = bucket_id.strip().lower()
        bucket = self.buckets.get(bid)
        if not bucket:
            raise FileNotFoundError(f"Bucket '{bid}' does not exist")

        clean_path = path.lstrip("/").replace("\\", "/")
        if not clean_path:
            raise ValueError("Object path cannot be empty")

        # Quota and file size validation
        if len(data) > bucket.file_size_limit_bytes:
            raise ValueError(f"File size exceeds bucket limit of {bucket.file_size_limit_bytes} bytes")

        if self.get_total_usage_bytes() + len(data) > self.quota_bytes:
            raise ValueError("Storage quota exceeded for this project")

        if not content_type:
            mime, _ = mimetypes.guess_type(clean_path)
            content_type = mime or "application/octet-stream"

        if bucket.allowed_mime_types and content_type not in bucket.allowed_mime_types:
            raise ValueError(f"MIME type '{content_type}' is not allowed in this bucket")

        dest_file = self.root_dir / bid / clean_path
        dest_file.parent.mkdir(parents=True, exist_ok=True)

        with open(dest_file, "wb") as f:
            f.write(data)

        etag = hashlib.sha256(data).hexdigest()
        now = time.time()
        obj_key = f"{bid}/{clean_path}"

        obj_info = {
            "name": clean_path,
            "bucket_id": bid,
            "size": len(data),
            "content_type": content_type,
            "etag": etag,
            "owner": owner_id or "anonymous",
            "created_at": now,
            "updated_at": now
        }
        self.objects[obj_key] = obj_info
        self._save_state()
        return obj_info

    def get_object(self, bucket_id: str, path: str) -> Tuple[bytes, str, Dict[str, Any]]:
        """Returns (data_bytes, content_type, metadata)."""
        bid = bucket_id.strip().lower()
        clean_path = path.lstrip("/").replace("\\", "/")
        obj_key = f"{bid}/{clean_path}"
        obj_info = self.objects.get(obj_key)

        file_path = self.root_dir / bid / clean_path
        if not file_path.exists():
            raise FileNotFoundError(f"Object '{clean_path}' not found in bucket '{bid}'")

        with open(file_path, "rb") as f:
            data = f.read()

        content_type = obj_info.get("content_type", "application/octet-stream") if obj_info else "application/octet-stream"
        return data, content_type, obj_info or {}

    def delete_object(self, bucket_id: str, path: str) -> bool:
        bid = bucket_id.strip().lower()
        clean_path = path.lstrip("/").replace("\\", "/")
        obj_key = f"{bid}/{clean_path}"

        file_path = self.root_dir / bid / clean_path
        if file_path.exists():
            file_path.unlink()

        if obj_key in self.objects:
            del self.objects[obj_key]
            self._save_state()
        return True

    def list_objects(self, bucket_id: str, prefix: str = "", limit: int = 100) -> List[Dict[str, Any]]:
        bid = bucket_id.strip().lower()
        if bid not in self.buckets:
            raise FileNotFoundError(f"Bucket '{bid}' not found")

        clean_prefix = prefix.lstrip("/").replace("\\", "/")
        results = []
        for key, info in self.objects.items():
            if key.startswith(f"{bid}/"):
                sub_path = key[len(bid) + 1:]
                if not clean_prefix or sub_path.startswith(clean_prefix):
                    results.append(info)
                    if len(results) >= limit:
                        break
        return results

    # --- Signed URL Generation & Verification ---

    def create_signed_url(self, bucket_id: str, path: str, expires_in_seconds: int = 3600) -> str:
        """Create a cryptographic token permitting download of private objects."""
        bid = bucket_id.strip().lower()
        clean_path = path.lstrip("/").replace("\\", "/")
        exp = int(time.time() + expires_in_seconds)

        payload = {
            "iss": "ceebase_storage",
            "bucket": bid,
            "path": clean_path,
            "exp": exp
        }
        token = encode_jwt(payload, self.signing_secret)
        return token

    def verify_signed_url(self, bucket_id: str, path: str, token: str) -> bool:
        bid = bucket_id.strip().lower()
        clean_path = path.lstrip("/").replace("\\", "/")

        valid, payload, err = decode_jwt(token, self.signing_secret)
        if not valid or not payload:
            return False

        if payload.get("bucket") != bid or payload.get("path") != clean_path:
            return False

        return True
