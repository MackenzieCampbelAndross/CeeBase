"""
CeeBase Platform JWT Utilities
Provides secure HS256 JSON Web Token generation, signature verification,
and claim decoding without external dependencies.
"""

import base64
import hashlib
import hmac
import json
import time
from typing import Any, Dict, Optional, Tuple


def _b64url_encode(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).rstrip(b'=').decode('ascii')


def _b64url_decode(data: str) -> bytes:
    padding = 4 - (len(data) % 4)
    if padding != 4:
        data += '=' * padding
    return base64.urlsafe_b64decode(data.encode('ascii'))


def encode_jwt(payload: Dict[str, Any], secret: str, algorithm: str = "HS256") -> str:
    """Encode payload into signed JWT token."""
    header = {"alg": algorithm, "typ": "JWT"}
    header_json = json.dumps(header, separators=(',', ':'), sort_keys=True).encode('utf-8')
    payload_json = json.dumps(payload, separators=(',', ':'), sort_keys=True).encode('utf-8')

    segments = [
        _b64url_encode(header_json),
        _b64url_encode(payload_json)
    ]
    signing_input = f"{segments[0]}.{segments[1]}".encode('ascii')
    signature = hmac.new(secret.encode('utf-8'), signing_input, hashlib.sha256).digest()
    segments.append(_b64url_encode(signature))
    return ".".join(segments)


def decode_jwt(token: str, secret: str, verify_exp: bool = True) -> Tuple[bool, Optional[Dict[str, Any]], str]:
    """
    Decode and verify signed JWT token.
    Returns (is_valid, payload, error_message).
    """
    try:
        parts = token.strip().split('.')
        if len(parts) != 3:
            return False, None, "Invalid token format: expected 3 segments"

        header_b64, payload_b64, signature_b64 = parts
        signing_input = f"{header_b64}.{payload_b64}".encode('ascii')
        expected_sig = hmac.new(secret.encode('utf-8'), signing_input, hashlib.sha256).digest()
        actual_sig = _b64url_decode(signature_b64)

        if not hmac.compare_digest(expected_sig, actual_sig):
            return False, None, "Signature verification failed"

        header = json.loads(_b64url_decode(header_b64).decode('utf-8'))
        if header.get("alg") != "HS256":
            return False, None, f"Unsupported algorithm: {header.get('alg')}"

        payload = json.loads(_b64url_decode(payload_b64).decode('utf-8'))

        if verify_exp and "exp" in payload:
            if time.time() > payload["exp"]:
                return False, None, "Token has expired"

        return True, payload, "OK"
    except Exception as e:
        return False, None, f"Token decode error: {str(e)}"


def create_project_tokens(project_id: str, jwt_secret: str) -> Dict[str, str]:
    """Generate the standard anon public key and service_role secret key for a project."""
    now = int(time.time())
    one_year = now + 365 * 24 * 3600

    anon_payload = {
        "iss": "ceebase",
        "sub": project_id,
        "ref": project_id,
        "role": "anon",
        "iat": now,
        "exp": one_year
    }

    service_payload = {
        "iss": "ceebase",
        "sub": project_id,
        "ref": project_id,
        "role": "service_role",
        "iat": now,
        "exp": one_year
    }

    return {
        "anon_key": encode_jwt(anon_payload, jwt_secret),
        "service_role_key": encode_jwt(service_payload, jwt_secret)
    }
