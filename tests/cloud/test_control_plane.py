#!/usr/bin/env python3
"""
Automated Test Suite for CeeBase Cloud Control Plane
Tests:
- Organization registration & plan quotas.
- Idempotent provisioning of shared multi-tenant and dedicated deployments.
- Resource allocation and limits.
- Credential rotation.
- Suspend, resume, and safe deprovisioning with archival snapshot.
- Usage metering & audit log persistence.
"""

import os
import shutil
import sys
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(REPO_ROOT))
sys.path.insert(0, str(REPO_ROOT / "bindings" / "python"))

from src.cloud.control_plane import CloudControlPlane, DeploymentModel, DeploymentStatus


class TestCloudControlPlane(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.test_dir = REPO_ROOT / "test_cloud_sandbox"
        cls.test_dir.mkdir(exist_ok=True)
        cls.cp = CloudControlPlane(str(cls.test_dir))

    @classmethod
    def tearDownClass(cls):
        if cls.test_dir.exists():
            shutil.rmtree(cls.test_dir, ignore_errors=True)

    def test_01_organization_registration(self):
        org = self.cp.create_organization(
            name="Acme Global",
            slug="acme-global",
            billing_email="billing@acme.com",
            plan="pro"
        )
        self.assertEqual(org["org_id"], "org_acme_global")
        self.assertEqual(org["plan"], "pro")
        self.assertEqual(org["quotas"]["max_deployments"], 50)

        # Idempotent call returns existing org
        org2 = self.cp.create_organization("Acme Global", "acme-global", "billing@acme.com")
        self.assertEqual(org2["org_id"], org["org_id"])

    def test_02_shared_deployment_provisioning(self):
        dep = self.cp.provision_deployment(
            org_id="org_acme_global",
            deployment_name="production-shared",
            model=DeploymentModel.SHARED
        )
        self.assertEqual(dep["status"], DeploymentStatus.ACTIVE)
        self.assertEqual(dep["model"], DeploymentModel.SHARED)
        self.assertIn("cb_live_", dep["auth_token"])
        self.assertTrue(os.path.exists(dep["database_file"]))

        # Idempotent re-provision returns same record
        dep2 = self.cp.provision_deployment("org_acme_global", "production-shared")
        self.assertEqual(dep2["deployment_id"], dep["deployment_id"])

    def test_03_dedicated_deployment_provisioning(self):
        dep = self.cp.provision_deployment(
            org_id="org_acme_global",
            deployment_name="analytics-dedicated",
            model=DeploymentModel.DEDICATED,
            custom_limits={"cpu": "4.0", "memory_mb": 8192}
        )
        self.assertEqual(dep["model"], DeploymentModel.DEDICATED)
        self.assertEqual(dep["resources"]["cpu_limit"], "4.0")
        self.assertIsNotNone(dep["resources"]["dedicated_port"])

    def test_04_credential_rotation(self):
        dep_id = "dep_org_acme_global_production_shared"
        old_token = self.cp.deployments[dep_id]["auth_token"]
        new_token = self.cp.rotate_credentials(dep_id)

        self.assertNotEqual(old_token, new_token)
        self.assertEqual(self.cp.deployments[dep_id]["auth_token"], new_token)

    def test_05_suspend_and_resume_lifecycle(self):
        dep_id = "dep_org_acme_global_production_shared"
        self.cp.suspend_deployment(dep_id)
        self.assertEqual(self.cp.deployments[dep_id]["status"], DeploymentStatus.SUSPENDED)

        self.cp.resume_deployment(dep_id)
        self.assertEqual(self.cp.deployments[dep_id]["status"], DeploymentStatus.ACTIVE)

    def test_06_usage_metering_without_paywall(self):
        dep_id = "dep_org_acme_global_production_shared"
        self.cp.record_usage(dep_id, queries=150, storage_bytes=65536)

        summary = self.cp.get_organization_usage("org_acme_global")
        self.assertGreaterEqual(summary["total_queries"], 150)
        self.assertGreaterEqual(summary["total_storage_bytes"], 65536)

    def test_07_safe_deprovisioning_with_archival(self):
        dep_id = "dep_org_acme_global_production_shared"
        db_file = self.cp.deployments[dep_id]["database_file"]
        self.assertTrue(os.path.exists(db_file))

        success = self.cp.deprovision_deployment(dep_id)
        self.assertTrue(success)
        self.assertEqual(self.cp.deployments[dep_id]["status"], DeploymentStatus.DEPROVISIONED)
        # Database file removed from active pool
        self.assertFalse(os.path.exists(db_file))
        # Archival snapshot created
        archive_file = self.test_dir / "archives" / f"{dep_id}_pre_delete.cbdb"
        self.assertTrue(archive_file.exists())

    def test_08_audit_trail_recorded(self):
        audit_file = self.test_dir / "cloud_audit.jsonl"
        self.assertTrue(audit_file.exists())
        with open(audit_file, "r", encoding="utf-8") as f:
            lines = f.readlines()
        self.assertGreater(len(lines), 3)


if __name__ == "__main__":
    unittest.main()
