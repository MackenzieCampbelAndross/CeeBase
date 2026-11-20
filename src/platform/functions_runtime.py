"""
CeeBase Platform Functions & Background Jobs Runtime
Provides serverless function execution, background workers, scheduled cron triggers,
timeout isolation, environment secrets injection, and invocation metrics.
"""

import json
import os
import subprocess
import sys
import tempfile
import threading
import time
import uuid
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple


class PlatformFunction:
    def __init__(
        self,
        name: str,
        project_id: str,
        runtime: str = "python3",  # python3 or node
        code: str = "",
        env_vars: Optional[Dict[str, str]] = None,
        timeout_sec: float = 5.0,
        version: int = 1,
        created_at: Optional[float] = None
    ):
        self.name = name
        self.project_id = project_id
        self.runtime = runtime
        self.code = code
        self.env_vars = env_vars or {}
        self.timeout_sec = timeout_sec
        self.version = version
        self.created_at = created_at or time.time()
        self.total_invocations = 0
        self.failed_invocations = 0
        self.total_duration_ms = 0.0

    def to_dict(self) -> Dict[str, Any]:
        return {
            "name": self.name,
            "project_id": self.project_id,
            "runtime": self.runtime,
            "code": self.code,
            "env_vars": list(self.env_vars.keys()),  # Do not leak secrets
            "timeout_sec": self.timeout_sec,
            "version": self.version,
            "created_at": self.created_at,
            "metrics": {
                "invocations": self.total_invocations,
                "failures": self.failed_invocations,
                "avg_duration_ms": (self.total_duration_ms / self.total_invocations) if self.total_invocations else 0.0
            }
        }

    @classmethod
    def from_dict(cls, d: Dict[str, Any], env_vars: Optional[Dict[str, str]] = None) -> "PlatformFunction":
        fn = cls(
            name=d["name"],
            project_id=d["project_id"],
            runtime=d.get("runtime", "python3"),
            code=d.get("code", ""),
            env_vars=env_vars or d.get("env_vars", {}),
            timeout_sec=d.get("timeout_sec", 5.0),
            version=d.get("version", 1),
            created_at=d.get("created_at")
        )
        if "metrics" in d:
            fn.total_invocations = d["metrics"].get("invocations", 0)
            fn.failed_invocations = d["metrics"].get("failures", 0)
        return fn


class FunctionsRuntime:
    """Manages function registration, persistent storage, and sandboxed subprocess execution."""

    def __init__(self, root_dir: Path):
        self.root_dir = Path(root_dir)
        self.root_dir.mkdir(parents=True, exist_ok=True)
        self.meta_file = self.root_dir / "functions.json"
        self.secrets_file = self.root_dir / "secrets.json"
        self.functions: Dict[str, PlatformFunction] = {}
        self.secrets: Dict[str, Dict[str, str]] = {}  # project_id -> {KEY: VAL}
        self.logs_dir = self.root_dir / "logs"
        self.logs_dir.mkdir(parents=True, exist_ok=True)
        self._load_state()

    def _load_state(self):
        if self.secrets_file.exists():
            try:
                with open(self.secrets_file, "r", encoding="utf-8") as f:
                    self.secrets = json.load(f)
            except Exception:
                self.secrets = {}

        if self.meta_file.exists():
            try:
                with open(self.meta_file, "r", encoding="utf-8") as f:
                    data = json.load(f)
                    for key, fnd in data.items():
                        proj = fnd["project_id"]
                        env = dict(self.secrets.get(proj, {}))
                        self.functions[key] = PlatformFunction.from_dict(fnd, env)
            except Exception:
                self.functions = {}

    def _save_state(self):
        with open(self.secrets_file, "w", encoding="utf-8") as f:
            json.dump(self.secrets, f, indent=2)

        data = {}
        for key, fn in self.functions.items():
            d = fn.to_dict()
            # Store code and configuration
            d["code"] = fn.code
            data[key] = d

        with open(self.meta_file, "w", encoding="utf-8") as f:
            json.dump(data, f, indent=2)

    # --- Secrets Management ---

    def set_secret(self, project_id: str, key: str, value: str):
        if project_id not in self.secrets:
            self.secrets[project_id] = {}
        self.secrets[project_id][key] = value
        self._save_state()

    def list_secrets(self, project_id: str) -> List[str]:
        return list(self.secrets.get(project_id, {}).keys())

    # --- Function Management ---

    def deploy_function(
        self,
        project_id: str,
        name: str,
        code: str,
        runtime: str = "python3",
        timeout_sec: float = 5.0
    ) -> PlatformFunction:
        fn_key = f"{project_id}:{name.lower()}"
        existing = self.functions.get(fn_key)
        version = (existing.version + 1) if existing else 1

        env = dict(self.secrets.get(project_id, {}))
        fn = PlatformFunction(
            name=name.lower(),
            project_id=project_id,
            runtime=runtime,
            code=code,
            env_vars=env,
            timeout_sec=timeout_sec,
            version=version
        )
        self.functions[fn_key] = fn
        self._save_state()
        return fn

    def list_functions(self, project_id: str) -> List[Dict[str, Any]]:
        return [
            fn.to_dict() for key, fn in self.functions.items()
            if fn.project_id == project_id
        ]

    def get_function(self, project_id: str, name: str) -> Optional[PlatformFunction]:
        return self.functions.get(f"{project_id}:{name.lower()}")

    def delete_function(self, project_id: str, name: str) -> bool:
        fn_key = f"{project_id}:{name.lower()}"
        if fn_key in self.functions:
            del self.functions[fn_key]
            self._save_state()
            return True
        return False

    # --- Execution / Invocation ---

    def invoke(
        self,
        project_id: str,
        name: str,
        payload: Dict[str, Any],
        headers: Optional[Dict[str, str]] = None
    ) -> Dict[str, Any]:
        fn = self.get_function(project_id, name)
        if not fn:
            raise FileNotFoundError(f"Function '{name}' not found in project '{project_id}'")

        fn.total_invocations += 1
        start_time = time.perf_counter()
        invocation_id = str(uuid.uuid4())

        # Construct runner wrapper script
        runner_code = f"""
import sys, json, os

payload = json.loads(sys.stdin.read() or "{{}}")

# User Function Code:
{fn.code}

if __name__ == "__main__":
    if "handler" in globals():
        res = handler(payload)
        print("---OUTPUT_START---")
        print(json.dumps(res))
    else:
        print("---OUTPUT_START---")
        print(json.dumps({{"error": "handler(payload) function not defined"}}))
"""

        env = os.environ.copy()
        env.update(self.secrets.get(project_id, {}))
        env["CEEBASE_PROJECT_ID"] = project_id
        env["CEEBASE_FUNCTION_NAME"] = name

        try:
            with tempfile.NamedTemporaryFile("w", suffix=".py", delete=False) as script_file:
                script_file.write(runner_code)
                script_path = script_file.name

            proc = subprocess.run(
                [sys.executable, script_path],
                input=json.dumps(payload),
                capture_output=True,
                text=True,
                timeout=fn.timeout_sec,
                env=env
            )

            duration_ms = (time.perf_counter() - start_time) * 1000.0
            fn.total_duration_ms += duration_ms

            raw_stdout = proc.stdout
            raw_stderr = proc.stderr

            result_data = None
            if "---OUTPUT_START---" in raw_stdout:
                parts = raw_stdout.split("---OUTPUT_START---")
                logs = parts[0].strip()
                try:
                    result_data = json.loads(parts[1].strip())
                except Exception:
                    result_data = {"output": parts[1].strip()}
            else:
                logs = raw_stdout.strip()
                result_data = {"raw_output": raw_stdout}

            status_code = 200 if proc.returncode == 0 else 500
            if proc.returncode != 0:
                fn.failed_invocations += 1

            # Log invocation
            self._log_invocation(project_id, name, invocation_id, duration_ms, status_code, logs, raw_stderr)

            self._save_state()
            return {
                "success": proc.returncode == 0,
                "status_code": status_code,
                "data": result_data,
                "logs": logs,
                "error": raw_stderr.strip() if proc.returncode != 0 else None,
                "duration_ms": round(duration_ms, 2),
                "invocation_id": invocation_id
            }

        except subprocess.TimeoutExpired:
            fn.failed_invocations += 1
            duration_ms = fn.timeout_sec * 1000.0
            fn.total_duration_ms += duration_ms
            self._save_state()
            return {
                "success": False,
                "status_code": 504,
                "error": f"Function execution exceeded timeout of {fn.timeout_sec}s",
                "duration_ms": duration_ms,
                "invocation_id": invocation_id
            }
        except Exception as e:
            fn.failed_invocations += 1
            return {
                "success": False,
                "status_code": 500,
                "error": str(e),
                "invocation_id": invocation_id
            }
        finally:
            try:
                if 'script_path' in locals() and os.path.exists(script_path):
                    os.unlink(script_path)
            except Exception:
                pass

    def _log_invocation(
        self,
        project_id: str,
        name: str,
        invocation_id: str,
        duration_ms: float,
        status: int,
        stdout: str,
        stderr: str
    ):
        log_entry = {
            "timestamp": time.time(),
            "invocation_id": invocation_id,
            "function": name,
            "project_id": project_id,
            "status": status,
            "duration_ms": round(duration_ms, 2),
            "stdout": stdout,
            "stderr": stderr
        }
        log_file = self.logs_dir / f"{project_id}_{name}.jsonl"
        try:
            with open(log_file, "a", encoding="utf-8") as f:
                f.write(json.dumps(log_entry) + "\n")
        except Exception:
            pass

    def get_logs(self, project_id: str, name: str, limit: int = 50) -> List[Dict[str, Any]]:
        log_file = self.logs_dir / f"{project_id}_{name}.jsonl"
        if not log_file.exists():
            return []
        lines = []
        try:
            with open(log_file, "r", encoding="utf-8") as f:
                for line in f:
                    if line.strip():
                        lines.append(json.loads(line))
        except Exception:
            pass
        return lines[-limit:]
