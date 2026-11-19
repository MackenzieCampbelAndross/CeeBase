# CeeBase Security, Identity & Multi-Tenancy Architecture

## 1. Overview
CeeBase Identity and Multi-Tenancy provides enterprise-grade authentication, authorization, and organization isolation built directly on the CeeBase kernel.

The subsystem supports two deployment models:
1. **Shared Multi-Tenant:** Multiple organizations share database server resources with strict database-level and session-level isolation.
2. **Dedicated Deployment:** Automated provisioning of isolated storage volumes, configuration files, and network ports for enterprise tenants.

---

## 2. Core Entities & System Database (`_ceebase_system.cbdb`)

All identity and access management state is maintained inside an internal, protected database instance: `_ceebase_system.cbdb`. Normal clients are forbidden by the kernel from selecting, modifying, or querying `_ceebase_system`.

### System Tables
- `sys_users`: User identities (`id`, `email`, `password_hash`, `status`, `failed_attempts`, `locked_until_ts`).
- `sys_organizations`: Tenant definitions (`id`, `name`, `slug`, `status`, `max_databases`, `max_storage_bytes`, `max_qpm`).
- `sys_memberships`: Role bindings connecting users to organizations.
- `sys_sessions`: Active bearer sessions (`token_hash`, `user_id`, `org_id`, `role`, `expires_at_ts`, `revoked`).
- `sys_api_keys`: Hashed API keys (`key_prefix`, `key_hash`, `org_id`, `actor_id`, `role`, `expires_at_ts`, `revoked`).
- `sys_service_accounts`: Machine identities for automated pipelines.
- `sys_agent_identities`: Authenticated identities for autonomous AI agents.
- `sys_audit_events`: Tamper-resistant security and operational log.
- `sys_otp_challenges`: Short-lived single-use one-time passwords for passwordless authentication.
- `sys_db_assignments`: Mapping identifying which organization owns which database.

---

## 3. Cryptography & Authentication

- **Password Hashing:** PBKDF2-HMAC-SHA256 with 16-byte cryptographically secure random salt, iteration factor, and constant-time hash verification. Format: `$pbkdf2-sha256$i=<iter>$s=<salt>$h=<hash>`. Plaintext passwords are never stored.
- **Brute Force Lockout:** 5 consecutive failed login attempts trigger an automatic 15-minute account lock.
- **Passwordless OTP:** 6-digit numeric codes generated from system CSPRNG, hashed on disk, valid for 5 minutes, single-use only, and revoked after 3 failed attempts.
- **API Keys:** Prefixed format `cb_live_<hex>`. The raw secret is displayed exactly once upon generation. Only the SHA-256 hash is persisted in `sys_api_keys`.
- **Session Tokens:** Format `cbs_<hex>`. Persisted solely as SHA-256 digests.
- **Break-Glass Emergency Protocol:** Local administrative token (`cb_breakglass_<hex>`) allows emergency credential reset and unlock of `admin@ceebase.local` when identity configuration requires offline recovery.
- **Fail-Closed Guarantee:** If the underlying system storage fails or cannot be recovered, all authentication requests immediately fail closed with `INTERNAL_ERROR`.

---

## 4. Role-Based Access Control (RBAC)

| Role | Database Management | DDL (Tables / Indexes) | DML (Read) | DML (Insert/Update/Delete) | User Management |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **PLATFORM_ADMIN** | Global | Global | Global | Global | Global |
| **ORG_OWNER** | Org-Scoped | Org-Scoped | Org-Scoped | Org-Scoped | Org-Scoped |
| **ORG_ADMIN** | Org-Scoped | Org-Scoped | Org-Scoped | Org-Scoped | Org-Scoped |
| **DB_ADMIN** | Connect Only | Org-Scoped | Org-Scoped | Org-Scoped | Denied |
| **DEVELOPER** | Connect Only | Org-Scoped | Org-Scoped | Org-Scoped | Denied |
| **READ_ONLY_ANALYST** | Connect Only | Denied | Org-Scoped | Denied | Denied |
| **SERVICE_ACCOUNT** | Connect Only | Org-Scoped | Org-Scoped | Org-Scoped | Denied |
| **AI_AGENT** | Connect Only | Org-Scoped | Org-Scoped | Org-Scoped | Denied |

---

## 5. Tenant Isolation

### Shared Multi-Tenant Routing
- Every operation requires a valid `TenantContext` resolved from the session token or API key.
- The `IdentityManager` verifies database ownership via `sys_db_assignments`. If an organization attempts to open, query, or mutate a database belonging to another organization, access is blocked immediately with `UNAUTHORIZED` (`StatusCode::Unauthorized`) and logged as a `CROSS_TENANT_VIOLATION` audit event.
- Organization quotas (`max_databases`) prevent noisy-neighbor storage exhaustion.

### Dedicated Deployment Provisioning
- The `DedicatedDeploymentManager` creates isolated file system trees:
  - `<base_storage_dir>/<org_id>/data/`
  - `<base_storage_dir>/<org_id>/wal/`
  - `<base_storage_dir>/<org_id>/conf/ceebase_dedicated.conf`
- Each dedicated deployment operates on an assigned network port and storage volume.

---

## 6. Verification & Test Evidence

All 75 automated test suites pass with 100% success:
- `IdentityTest.CryptoPrimitivesValidation`: PASS
- `IdentityTest.PasswordRegistrationAndAuthentication`: PASS
- `IdentityTest.BruteForceLockoutProtection`: PASS
- `IdentityTest.PasswordlessOtpLifecycle`: PASS
- `IdentityTest.SessionTokenManagementAndRevocation`: PASS
- `IdentityTest.ApiKeyGenerationAndAuthentication`: PASS
- `IdentityTest.BreakGlassAdministrativeRecovery`: PASS
- `TenantIsolationTest.CrossTenantDatabaseAccessDenied`: PASS
- `TenantIsolationTest.SystemDatabaseDirectAccessForbidden`: PASS
- `TenantIsolationTest.RoleBasedAccessControlEnforcement`: PASS
- `TenantIsolationTest.OrganizationDatabaseQuotaEnforcement`: PASS
- `TenantIsolationTest.DedicatedDeploymentProvisioning`: PASS
- `AdversarialSecurityTest.ForgedTokenAttack`: PASS
- `AdversarialSecurityTest.PrivilegeEscalationPrevention`: PASS
- `AdversarialSecurityTest.AuditTrailTamperResistance`: PASS
- (Plus 60 core storage, buffer, recovery, and parser tests): PASS

---

## 7. Known Limitations & Production Guidance
- **Clustered Rate-Limiting:** Failed login attempt counters are tracked per-node in the current implementation. For multi-node distributed deployments, rate-limiting will coordinate via shared Raft state (planned for Phase 8).
- **Dedicated Process Supervisor:** `DedicatedDeploymentManager` provisions configuration and file structures; orchestration of systemd units or Docker containers is managed by the host deployment agent.
