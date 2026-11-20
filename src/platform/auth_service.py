"""
CeeBase Platform Authentication & Identity Service
Provides user registration, password hashing (PBKDF2-HMAC-SHA256), session management,
JWT issuance, passwordless email OTP, and refresh token rotation.
"""

import hashlib
import json
import os
import secrets
import time
import uuid
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

from src.platform.jwt_util import decode_jwt, encode_jwt


class AuthService:
    """Manages application end-users, sessions, and tokens for a CeeBase project."""

    def __init__(self, state_dir: Path, jwt_secret: str):
        self.state_dir = Path(state_dir)
        self.state_dir.mkdir(parents=True, exist_ok=True)
        self.users_file = self.state_dir / "auth_users.json"
        self.sessions_file = self.state_dir / "auth_sessions.json"
        self.otp_file = self.state_dir / "auth_otps.json"
        self.jwt_secret = jwt_secret

        self.users: Dict[str, Dict[str, Any]] = {}
        self.sessions: Dict[str, Dict[str, Any]] = {}
        self.otps: Dict[str, Dict[str, Any]] = {}
        self._load_state()

    def _load_state(self):
        if self.users_file.exists():
            try:
                with open(self.users_file, "r", encoding="utf-8") as f:
                    self.users = json.load(f)
            except Exception:
                self.users = {}
        if self.sessions_file.exists():
            try:
                with open(self.sessions_file, "r", encoding="utf-8") as f:
                    self.sessions = json.load(f)
            except Exception:
                self.sessions = {}
        if self.otp_file.exists():
            try:
                with open(self.otp_file, "r", encoding="utf-8") as f:
                    self.otps = json.load(f)
            except Exception:
                self.otps = {}

    def _save_state(self):
        with open(self.users_file, "w", encoding="utf-8") as f:
            json.dump(self.users, f, indent=2)
        with open(self.sessions_file, "w", encoding="utf-8") as f:
            json.dump(self.sessions, f, indent=2)
        with open(self.otp_file, "w", encoding="utf-8") as f:
            json.dump(self.otps, f, indent=2)

    @staticmethod
    def _hash_password(password: str, salt: Optional[bytes] = None) -> Tuple[str, str]:
        if salt is None:
            salt = secrets.token_bytes(16)
        salt_hex = salt.hex()
        pw_hash = hashlib.pbkdf2_hmac("sha256", password.encode("utf-8"), salt, 100_000).hex()
        return pw_hash, salt_hex

    def _verify_password(self, password: str, expected_hash: str, salt_hex: str) -> bool:
        salt = bytes.fromhex(salt_hex)
        pw_hash = hashlib.pbkdf2_hmac("sha256", password.encode("utf-8"), salt, 100_000).hex()
        return secrets.compare_digest(pw_hash, expected_hash)

    def sign_up(self, email: str, password: str, metadata: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
        """Register a new user in the project."""
        email_clean = email.strip().lower()
        for u in self.users.values():
            if u["email"].lower() == email_clean:
                raise ValueError("User already registered with this email")

        user_id = str(uuid.uuid4())
        pw_hash, salt_hex = self._hash_password(password)
        now = time.time()

        user = {
            "id": user_id,
            "email": email_clean,
            "password_hash": pw_hash,
            "salt": salt_hex,
            "metadata": metadata or {},
            "created_at": now,
            "confirmed_at": now,
            "banned": False
        }
        self.users[user_id] = user
        self._save_state()

        session = self._create_session(user_id)
        return {
            "user": self._sanitize_user(user),
            "session": session
        }

    def sign_in_with_password(self, email: str, password: str) -> Dict[str, Any]:
        """Authenticate user with email and password."""
        email_clean = email.strip().lower()
        user = None
        for u in self.users.values():
            if u["email"].lower() == email_clean:
                user = u
                break

        if not user or not self._verify_password(password, user["password_hash"], user["salt"]):
            raise PermissionError("Invalid login credentials")

        if user.get("banned", False):
            raise PermissionError("User account is suspended")

        session = self._create_session(user["id"])
        return {
            "user": self._sanitize_user(user),
            "session": session
        }

    def generate_otp(self, email: str) -> str:
        """Generate a single-use 6-digit OTP code for passwordless login."""
        email_clean = email.strip().lower()
        code = f"{secrets.randbelow(1_000_000):06d}"
        self.otps[email_clean] = {
            "code": code,
            "expires_at": time.time() + 600,  # 10 minutes
            "attempts": 0
        }
        self._save_state()
        return code

    def verify_otp(self, email: str, code: str) -> Dict[str, Any]:
        """Verify an OTP code and issue a session."""
        email_clean = email.strip().lower()
        otp_entry = self.otps.get(email_clean)
        if not otp_entry:
            raise PermissionError("No OTP request found for this email")

        if time.time() > otp_entry["expires_at"]:
            del self.otps[email_clean]
            self._save_state()
            raise PermissionError("OTP has expired")

        otp_entry["attempts"] += 1
        if otp_entry["attempts"] > 5:
            del self.otps[email_clean]
            self._save_state()
            raise PermissionError("Maximum OTP verification attempts exceeded")

        if not secrets.compare_digest(otp_entry["code"], code.strip()):
            self._save_state()
            raise PermissionError("Invalid OTP code")

        # OTP verified: remove it
        del self.otps[email_clean]

        # Find or create user
        user = None
        for u in self.users.values():
            if u["email"].lower() == email_clean:
                user = u
                break

        if not user:
            user_id = str(uuid.uuid4())
            pw_hash, salt_hex = self._hash_password(secrets.token_urlsafe(24))
            now = time.time()
            user = {
                "id": user_id,
                "email": email_clean,
                "password_hash": pw_hash,
                "salt": salt_hex,
                "metadata": {},
                "created_at": now,
                "confirmed_at": now,
                "banned": False
            }
            self.users[user_id] = user

        session = self._create_session(user["id"])
        self._save_state()
        return {
            "user": self._sanitize_user(user),
            "session": session
        }

    def refresh_session(self, refresh_token: str) -> Dict[str, Any]:
        """Exchange a valid refresh token for a new access token and rotated refresh token."""
        session_id = None
        session = None
        for sid, s in self.sessions.items():
            if s.get("refresh_token") == refresh_token and not s.get("revoked", False):
                session_id = sid
                session = s
                break

        if not session or time.time() > session["expires_at"]:
            raise PermissionError("Invalid or expired refresh token")

        user = self.users.get(session["user_id"])
        if not user or user.get("banned", False):
            raise PermissionError("User no longer valid")

        # Invalidate old session and create fresh session (Refresh Token Rotation)
        session["revoked"] = True
        new_session = self._create_session(user["id"])
        self._save_state()
        return {
            "user": self._sanitize_user(user),
            "session": new_session
        }

    def sign_out(self, session_or_access_token: str) -> bool:
        """Revoke a session."""
        # Try finding by refresh token or session id
        for sid, s in list(self.sessions.items()):
            if s.get("refresh_token") == session_or_access_token or sid == session_or_access_token:
                s["revoked"] = True
                self._save_state()
                return True

        # Try decoding JWT and revoking matching user's active sessions
        valid, payload, _ = decode_jwt(session_or_access_token, self.jwt_secret, verify_exp=False)
        if valid and payload and "sub" in payload:
            user_id = payload["sub"]
            for s in self.sessions.values():
                if s["user_id"] == user_id:
                    s["revoked"] = True
            self._save_state()
            return True
        return False

    def get_user_by_token(self, token: str) -> Dict[str, Any]:
        """Verify JWT and return user record."""
        valid, payload, err = decode_jwt(token, self.jwt_secret)
        if not valid or not payload:
            raise PermissionError(f"Unauthorized: {err}")

        role = payload.get("role", "anon")
        if role == "anon":
            return {"id": "anon", "role": "anon", "email": None}
        elif role == "service_role":
            return {"id": "service_role", "role": "service_role", "email": "admin@ceebase.internal"}

        user_id = payload.get("sub")
        user = self.users.get(user_id)
        if not user:
            raise PermissionError("User not found")
        if user.get("banned", False):
            raise PermissionError("User is banned")
        sanitized = self._sanitize_user(user)
        sanitized["role"] = "authenticated"
        return sanitized

    def list_users(self) -> List[Dict[str, Any]]:
        """List all users in project for administration."""
        return [self._sanitize_user(u) for u in self.users.values()]

    def _create_session(self, user_id: str) -> Dict[str, Any]:
        now = time.time()
        expires_at = now + 7 * 24 * 3600  # 7 days
        user = self.users[user_id]

        payload = {
            "iss": "ceebase",
            "sub": user_id,
            "email": user["email"],
            "role": "authenticated",
            "user_metadata": user.get("metadata", {}),
            "iat": int(now),
            "exp": int(now + 3600)  # 1 hour access token
        }
        access_token = encode_jwt(payload, self.jwt_secret)
        refresh_token = secrets.token_hex(32)

        session_id = str(uuid.uuid4())
        self.sessions[session_id] = {
            "id": session_id,
            "user_id": user_id,
            "refresh_token": refresh_token,
            "created_at": now,
            "expires_at": expires_at,
            "revoked": False
        }
        self._save_state()

        return {
            "access_token": access_token,
            "token_type": "bearer",
            "expires_in": 3600,
            "refresh_token": refresh_token,
            "user": self._sanitize_user(user)
        }

    @staticmethod
    def _sanitize_user(user: Dict[str, Any]) -> Dict[str, Any]:
        return {
            "id": user["id"],
            "email": user["email"],
            "metadata": user.get("metadata", {}),
            "created_at": user.get("created_at"),
            "confirmed_at": user.get("confirmed_at"),
            "banned": user.get("banned", False)
        }
