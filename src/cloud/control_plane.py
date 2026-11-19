"""
CeeBase Cloud Control Plane
Manages multi-tenant organizations, shared vs dedicated deployments,
idempotent provisioning pipelines, credential rotation, usage metering, and audit trails.
"""

import hashlib
import json
import os
import shutil
import sys
import time
import uuid
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Dict, List, Optional

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(REPO_ROOT))
sys.path.insert(0, str(REPO_ROOT / "bindings" / "python"))

try:
    import ceebase
except ImportError:
    ceebase = None


class DeploymentModel:
    SHARED = "SHARED_MULTI_TENANT"
    DEDICATED = "DEDICATED"


class DeploymentStatus:
    PROVISIONING = "PROVISIONING"
    ACTIVE = "ACTIVE"
    SUSPENDED = "SUSPENDED"
    DEPROVISIONED = "DEPROVISIONED"


class CloudControlPlane:
    """Enterprise Control Plane for managed CeeBase deployments."""

    def __init__(self, state_dir: str = "cloud_state"):
        self.state_dir = Path(state_dir)
        self.state_dir.mkdir(parents=True, exist_ok=True)
        self.state_file = self.state_dir / "control_plane_state.json"
        self.audit_log = self.state_dir / "cloud_audit.jsonl"
        self.organizations: Dict[str, Dict[str, Any]] = {}
        self.deployments: Dict[str, Dict[str, Any]] = {}
        self.usage_meters: Dict[str, Dict[str, Any]] = {}
        self._load_state()

    def _load_state(self):
        if self.state_file.exists():
            try:
                with open(self.state_file, "r", encoding="utf-8") as f:
                    data = json.load(f)
                    self.organizations = data.get("organizations", {})
                    self.deployments = data.get("deployments", {})
                    self.usage_meters = data.get("usage_meters", {})
            except Exception:
                pass

    def _save_state(self):
        tmp_file = self.state_file.with_suffix(".tmp")
        payload = {
            "organizations": self.organizations,
            "deployments": self.deployments,
            "usage_meters": self.usage_meters,
            "updated_at": datetime.now(timezone.utc).isoformat()
        }
        with open(tmp_file, "w", encoding="utf-8") as f:
            json.dump(payload, f, indent=2)
        shutil.move(tmp_file, self.state_file)

    def log_audit(self, actor: str, action: str, target: str, details: Dict[str, Any], status: str = "SUCCESS"):
        entry = {
            "timestamp": datetime.now(timezone.utc).isoformat(),
            "actor": actor,
            "action": action,
            "target": target,
            "details": details,
            "status": status
        }
        try:
            with open(self.audit_log, "a", encoding="utf-8") as f:
                f.write(json.dumps(entry) + "\n")
        except Exception:
            pass

    # --- Organization Management ---

    def create_organization(
        self,
        name: str,
        slug: str,
        billing_email: str,
        plan: str = "free",
        actor: str = "system"
    ) -> Dict[str, Any]:
        """Registers a new tenant organization."""
        org_id = f"org_{slug.replace('-', '_')}"
        if org_id in self.organizations:
            return self.organizations[org_id]

        org_record = {
            "org_id": org_id,
            "name": name,
            "slug": slug,
            "billing_email": billing_email,
            "plan": plan,
            "quotas": {
                "max_deployments": 5 if plan == "free" else (50 if plan == "pro" else 500),
                "max_storage_mb": 1024 if plan == "free" else 51200,
                "max_qps": 100 if plan == "free" else 5000,
            },
            "created_at": datetime.now(timezone.utc).isoformat()
        }

        self.organizations[org_id] = org_record
        self._save_state()
        self.log_audit(actor, "ORG_CREATE", org_id, {"name": name, "plan": plan})
        return org_record

    # --- Idempotent Deployment Provisioning ---

    def provision_deployment(
        self,
        org_id: str,
        deployment_name: str,
        model: str = DeploymentModel.SHARED,
        custom_limits: Optional[Dict[str, Any]] = None,
        actor: str = "system"
    ) -> Dict[str, Any]:
        """Provisions an isolated database deployment (shared multi-tenant or dedicated)."""
        if org_id not in self.organizations:
            raise ValueError(f"Organization '{org_id}' does not exist")

        dep_id = f"dep_{org_id}_{deployment_name.replace('-', '_')}"
        # Idempotency check: if deployment already exists and is active, return it
        if dep_id in self.deployments:
            existing = self.deployments[dep_id]
            if existing["status"] == DeploymentStatus.ACTIVE:
                return existing

        # Check quotas
        active_count = sum(1 for d in self.deployments.values() if d["org_id"] == org_id and d["status"] != DeploymentStatus.DEPROVISIONED)
        quota_max = self.organizations[org_id]["quotas"]["max_deployments"]
        if active_count >= quota_max:
            raise PermissionError(f"Quota Exceeded: Organization '{org_id}' has reached deployment quota ({quota_max})")

        # Setup isolation path
        db_filename = f"{dep_id}.cbdb"
        db_path = str(self.state_dir / db_filename)

        # Initialize physical database kernel storage
        if ceebase is not None and not os.path.exists(db_path):
            conn = ceebase.connect(db_path)
            conn.execute("CREATE TABLE _ceebase_meta (id INT PRIMARY KEY, meta_key VARCHAR, meta_val VARCHAR);")
            conn.execute(f"INSERT INTO _ceebase_meta VALUES (1, 'tenant_org', '{org_id}');")
            conn.close()

        # Generate cryptographic token for tenant access
        token = f"cb_live_{uuid.uuid4().hex}"

        resource_allocation = {
            "model": model,
            "cpu_limit": custom_limits.get("cpu", "1.0") if custom_limits else ("0.5" if model == DeploymentModel.SHARED else "2.0"),
            "memory_limit_mb": custom_limits.get("memory_mb", 512) if custom_limits else (512 if model == DeploymentModel.SHARED else 4096),
            "storage_limit_mb": custom_limits.get("storage_mb", 1024) if custom_limits else 10240,
            "dedicated_port": None if model == DeploymentModel.SHARED else (8090 + active_count)
        }

        deployment_record = {
            "deployment_id": dep_id,
            "org_id": org_id,
            "name": deployment_name,
            "model": model,
            "status": DeploymentStatus.ACTIVE,
            "database_file": db_path,
            "auth_token": token,
            "resources": resource_allocation,
            "created_at": datetime.now(timezone.utc).isoformat(),
            "updated_at": datetime.now(timezone.utc).isoformat()
        }

        self.deployments[dep_id] = deployment_record
        self.usage_meters[dep_id] = {
            "query_count": 0,
            "storage_bytes": os.path.getsize(db_path) if os.path.exists(db_path) else 0,
            "active_connections": 1,
            "backup_bytes": 0,
            "uptime_seconds": 0
        }

        self._save_state()
        self.log_audit(actor, "DEPLOYMENT_PROVISION", dep_id, {"model": model, "org_id": org_id})
        return deployment_record

    def rotate_credentials(self, deployment_id: str, actor: str = "system") -> str:
        """Rotates API access token for a deployment, invalidating the previous token."""
        if deployment_id not in self.deployments:
            raise ValueError(f"Deployment '{deployment_id}' not found")

        new_token = f"cb_live_{uuid.uuid4().hex}"
        self.deployments[deployment_id]["auth_token"] = new_token
        self.deployments[deployment_id]["updated_at"] = datetime.now(timezone.utc).isoformat()
        self._save_state()

        self.log_audit(actor, "CREDENTIAL_ROTATE", deployment_id, {"status": "ROTATED"})
        return new_token

    def suspend_deployment(self, deployment_id: str, actor: str = "system"):
        """Suspends deployment due to non-payment or administrative request."""
        if deployment_id not in self.deployments:
            raise ValueError(f"Deployment '{deployment_id}' not found")
        self.deployments[deployment_id]["status"] = DeploymentStatus.SUSPENDED
        self._save_state()
        self.log_audit(actor, "DEPLOYMENT_SUSPEND", deployment_id, {})

    def resume_deployment(self, deployment_id: str, actor: str = "system"):
        """Resumes suspended deployment."""
        if deployment_id not in self.deployments:
            raise ValueError(f"Deployment '{deployment_id}' not found")
        self.deployments[deployment_id]["status"] = DeploymentStatus.ACTIVE
        self._save_state()
        self.log_audit(actor, "DEPLOYMENT_RESUME", deployment_id, {})

    def deprovision_deployment(self, deployment_id: str, actor: str = "system") -> bool:
        """Safely deprovisions deployment with pre-deletion archival backup."""
        if deployment_id not in self.deployments:
            return False

        dep = self.deployments[deployment_id]
        db_path = Path(dep["database_file"])

        # Create archival snapshot before deletion
        archive_dir = self.state_dir / "archives"
        archive_dir.mkdir(exist_ok=True)
        if db_path.exists():
            archive_path = archive_dir / f"{deployment_id}_pre_delete.cbdb"
            shutil.copy2(db_path, archive_path)
            db_path.unlink()

        wal_path = db_path.with_suffix(".wal")
        if wal_path.exists():
            wal_path.unlink()

        dep["status"] = DeploymentStatus.DEPROVISIONED
        dep["updated_at"] = datetime.now(timezone.utc).isoformat()
        self._save_state()

        self.log_audit(actor, "DEPLOYMENT_DEPROVISION", deployment_id, {"archive": str(archive_dir)})
        return True

    # --- Usage Metering ---

    def record_usage(self, deployment_id: str, queries: int = 0, storage_bytes: Optional[int] = None):
        """Meters usage without any paywall enforcement in database engine."""
        if deployment_id in self.usage_meters:
            meter = self.usage_meters[deployment_id]
            meter["query_count"] += queries
            if storage_bytes is not None:
                meter["storage_bytes"] = storage_bytes
            self._save_state()

    def get_organization_usage(self, org_id: str) -> Dict[str, Any]:
        """Aggregates usage metrics across all deployments belonging to an organization."""
        org_deployments = [d for d in self.deployments.values() if d["org_id"] == org_id]
        total_queries = 0
        total_storage = 0

        for d in org_deployments:
            m = self.usage_meters.get(d["deployment_id"], {})
            total_queries += m.get("query_count", 0)
            total_storage += m.get("storage_bytes", 0)

        return {
            "org_id": org_id,
            "active_deployments": sum(1 for d in org_deployments if d["status"] == DeploymentStatus.ACTIVE),
            "total_queries": total_queries,
            "total_storage_bytes": total_storage,
            "total_storage_mb": round(total_storage / (1024 * 1024), 2)
        }
