# CeeBase Production Infrastructure & Kubernetes Deployment Guide

This document defines the deployment topologies, reliability engineering, backup policies, replication guarantees, and operational procedures for enterprise CeeBase deployments.

---

## 1. Production Topologies

CeeBase supports three official production topologies:

```
[ Standalone Single-Node ]        [ Active-Standby High Availability ]
┌────────────────────────┐        ┌──────────────────┐    WAL Stream    ┌──────────────────┐
│  ceebase-primary       │        │ ceebase-primary  │ ───────────────> │ ceebase-standby  │
│  StatefulSet (1 Node)  │        │ (Read/Write)     │                  │ (Read-Only)      │
│  Local PVC (50 GB)     │        └────────┬─────────┘                  └────────┬─────────┘
└────────────────────────┘                 │                                     │
                                    [PVC: Primary Data]                  [PVC: Standby Data]
```

### Topology A: Standalone Managed Deployment
- 1 Replica StatefulSet with persistent local SSD storage.
- Automated nightly encrypted backups mounted to shared NFS/S3 storage.
- Recommended for departmental, staging, and single-tenant applications.

### Topology B: Active-Standby Streaming HA Cluster
- Primary database node accepts transactional read/write workloads (`CEEBASE_ROLE=PRIMARY`).
- Streaming Standby replica continuously pulls and applies WAL slices (`CEEBASE_ROLE=STANDBY`).
- Zero data loss within same availability zone; read queries can be offloaded to Standby replica.
- Split-brain protection via lease epoch fencing.

---

## 2. Replication Architecture & Guarantees

- **WAL Shipping & Streaming**: Primary exposes `/api/v1/replication/wal?since_lsn=<lsn>`. The Standby continuously polls new log slices, persists them to local WAL, and triggers recovery replay against local slotted pages.
- **Lag Measurement**:
  - `ceebase_replication_lag_bytes = primary_current_lsn - standby_applied_lsn`
  - Real-time gauge exposed via Prometheus metrics.
- **Consistency Verification**:
  - Standby verifies page checksums and table row count parity with Primary.
- **Split-Brain Prevention (Epoch Fencing)**:
  - Every Primary node operates under an integer term epoch (`epoch = N`).
  - During controlled failover, Standby is promoted to Primary (`epoch = N + 1`).
  - The older Primary is immediately fenced into `FENCED` mode upon observing higher epoch traffic, rejecting write attempts with `Cluster Fenced: Split-brain protection active`.

---

## 3. Backup, Encryption & Point-in-Time Recovery (PITR)

- **Cryptographic Encryption**: Backups are encrypted using AES-256 with PBKDF2 (10,000 rounds) and authenticated HMAC-SHA256 integrity tagging.
- **Incremental Backups**:
  - Full Base Backup: complete disk page snapshot + base WAL.
  - Incremental Backup: log sequence slice from `base_lsn` to `current_lsn`.
- **Integrity Guarantee**:
  - Every backup is cataloged in `backup_catalog.json` with a SHA-256 digest.
  - A backup is strictly verified before restore (checksum verification, Page 0 header magic `0x43424442`, CRC-32 checksums).
- **Point-in-Time Recovery (PITR)**:
  - Restores Base Backup.
  - Progressively replays incremental WAL chains up to the requested target LSN or timestamp.
- **Automated Retention**:
  - Automated pruning retains the most recent $K$ backups (default: 7 daily snapshots).

---

## 4. Observability & Monitoring

The `/metrics` endpoint exposes production Prometheus metrics (`text/plain; version=0.0.4`):

| Metric | Type | Description |
| :--- | :--- | :--- |
| `ceebase_queries_total` | Counter | Total SQL queries executed |
| `ceebase_query_duration_seconds` | Histogram | High-precision query latency distribution |
| `ceebase_transactions_total` | Counter | Breakdown of commits vs rollbacks |
| `ceebase_wal_bytes_written_total` | Counter | Cumulative WAL write throughput |
| `ceebase_buffer_pool_hit_ratio` | Gauge | Cache hit ratio for 4 KB slotted pages |
| `ceebase_storage_bytes_total` | Gauge | Disk storage consumption in bytes |
| `ceebase_active_connections` | Gauge | Live client connections |
| `ceebase_replication_lag_bytes` | Gauge | Measured byte delta between Primary and Standby |
| `ceebase_backup_status` | Gauge | 1 for healthy last backup, 0 for failure |
| `ceebase_auth_failures_total` | Counter | Failed credential login attempts |
| `ceebase_tenant_queries_total` | Counter | Multi-tenant query volume per organization |

---

## 5. Kubernetes Deployment Instructions

```bash
# 1. Apply Namespace
kubectl apply -f deploy/k8s/00-namespace.yaml

# 2. Deploy ConfigMap & Secrets
kubectl apply -f deploy/k8s/01-configmap.yaml
kubectl apply -f deploy/k8s/02-secrets.yaml

# 3. Provision Storage Claims
kubectl apply -f deploy/k8s/03-storageclass-pvc.yaml

# 4. Deploy Primary & Standby StatefulSets
kubectl apply -f deploy/k8s/04-statefulset-primary.yaml
kubectl apply -f deploy/k8s/05-statefulset-standby.yaml

# 5. Expose Services & Protection Policies
kubectl apply -f deploy/k8s/06-service.yaml
kubectl apply -f deploy/k8s/07-poddisruptionbudget.yaml
kubectl apply -f deploy/k8s/08-backup-cronjob.yaml
```
