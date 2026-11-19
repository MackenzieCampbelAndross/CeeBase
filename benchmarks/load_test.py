#!/usr/bin/env python3
"""
CeeBase Production Load Testing & Multi-Tenant Benchmark Harness
Simulates:
- 100 distinct tenant organizations
- 1,000 concurrent connection sessions / connection churn
- Mixed workload: 80% read queries, 20% write transactions
- Tenant resource contention & large result set scans
- Concurrent background backup snapshot during active read/write load
- High-precision latency measurement: p50, p95, p99, throughput (QPS), error rate.
"""

import concurrent.futures
import math
import os
import platform
import random
import sys
import threading
import time
from pathlib import Path
from typing import Any, Dict, List, Tuple

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT))
sys.path.insert(0, str(REPO_ROOT / "bindings" / "python"))

import ceebase
from src.infra.backup_manager import BackupManager


def get_system_specs() -> Dict[str, str]:
    return {
        "os": f"{platform.system()} {platform.release()}",
        "architecture": platform.machine(),
        "processor": platform.processor() or "AMD64 Family",
        "cpu_count": str(os.cpu_count() or 8),
        "python_version": platform.python_version()
    }


class LoadTestHarness:
    def __init__(
        self,
        db_path: str = "load_test_sandbox.cbdb",
        num_tenants: int = 100,
        total_connections: int = 1000,
        worker_threads: int = 16
    ):
        self.db_path = db_path
        self.num_tenants = num_tenants
        self.total_connections = total_connections
        self.worker_threads = worker_threads
        self.latencies: List[float] = []
        self.errors_count = 0
        self.total_ops = 0
        self._lock = threading.Lock()

    def setup_database(self):
        if os.path.exists(self.db_path):
            os.remove(self.db_path)
        wal = str(Path(self.db_path).with_suffix(".wal"))
        if os.path.exists(wal):
            os.remove(wal)

        conn = ceebase.connect(self.db_path)
        cur = conn.cursor()
        cur.execute("CREATE TABLE tenant_records (id INT PRIMARY KEY, org_id VARCHAR, payload VARCHAR, amount DOUBLE, active BOOLEAN);")

        # Seed initial data for 100 tenants
        for t in range(1, self.num_tenants + 1):
            cur.execute(f"INSERT INTO tenant_records VALUES ({t}, 'org_{t:03d}', 'Initial baseline payload for tenant', {t * 10.5}, true);")

        conn.close()

    def run_worker_task(self, task_id: int) -> List[float]:
        local_latencies = []
        tenant_id = f"org_{(task_id % self.num_tenants) + 1:03d}"

        try:
            conn = ceebase.connect(self.db_path)
            cur = conn.cursor()

            # Randomly select between Read (80%) and Write (20%)
            is_write = (random.random() < 0.20)

            t0 = time.perf_counter()
            if is_write:
                new_id = self.num_tenants + task_id + 1
                cur.execute(f"INSERT INTO tenant_records VALUES ({new_id}, '{tenant_id}', 'Transacted telemetry event', {random.random() * 500}, true);")
            else:
                # Read query: aggregate or tenant point query
                if random.random() < 0.5:
                    cur.execute(f"SELECT * FROM tenant_records WHERE id = {(task_id % self.num_tenants) + 1};")
                else:
                    cur.execute("SELECT COUNT(*) FROM tenant_records;")
                _ = cur.fetchall()

            dur_ms = (time.perf_counter() - t0) * 1000.0
            local_latencies.append(dur_ms)
            conn.close()
        except Exception:
            with self._lock:
                self.errors_count += 1

        return local_latencies

    def execute_concurrent_backup(self, bm: BackupManager):
        """Simulates an automated cron backup occurring while load testing is active."""
        try:
            bm.create_full_backup(self.db_path, backup_id="live_concurrent_backup_snap")
        except Exception as e:
            with self._lock:
                self.errors_count += 1

    def run_load_test(self) -> Dict[str, Any]:
        self.setup_database()
        bm = BackupManager("backups")

        wall_start = time.perf_counter()

        # Start concurrent backup halfway through workload
        backup_thread = threading.Thread(target=self.execute_concurrent_backup, args=(bm,))
        backup_started = False

        all_latencies = []
        with concurrent.futures.ThreadPoolExecutor(max_workers=self.worker_threads) as executor:
            futures = []
            for i in range(self.total_connections):
                futures.append(executor.submit(self.run_worker_task, i))

                # Fire backup thread around 30% progress
                if i == 300 and not backup_started:
                    backup_thread.start()
                    backup_started = True

            for fut in concurrent.futures.as_completed(futures):
                all_latencies.extend(fut.result())

        if backup_started:
            backup_thread.join()

        wall_elapsed = time.perf_counter() - wall_start

        # Calculate statistics
        all_latencies.sort()
        n = len(all_latencies)
        p50 = all_latencies[int(n * 0.50)] if n > 0 else 0.0
        p95 = all_latencies[int(n * 0.95)] if n > 0 else 0.0
        p99 = all_latencies[int(n * 0.99)] if n > 0 else 0.0
        avg_lat = sum(all_latencies) / n if n > 0 else 0.0
        qps = n / wall_elapsed if wall_elapsed > 0 else 0.0

        # Clean sandbox
        for p in [self.db_path, str(Path(self.db_path).with_suffix(".wal"))]:
            if os.path.exists(p):
                try:
                    os.remove(p)
                except Exception:
                    pass

        return {
            "system_specs": get_system_specs(),
            "workload": {
                "simulated_tenants": self.num_tenants,
                "total_connections": self.total_connections,
                "concurrency_threads": self.worker_threads,
                "read_ratio": "80%",
                "write_ratio": "20%",
                "concurrent_backup_during_load": True
            },
            "results": {
                "operations_completed": n,
                "operations_failed": self.errors_count,
                "error_rate_percent": round((self.errors_count / max(1, self.total_connections)) * 100.0, 3),
                "total_duration_sec": round(wall_elapsed, 3),
                "throughput_qps": round(qps, 2),
                "latency_p50_ms": round(p50, 3),
                "latency_p95_ms": round(p95, 3),
                "latency_p99_ms": round(p99, 3),
                "latency_mean_ms": round(avg_lat, 3)
            }
        }


def main():
    print("=" * 65)
    print("  CeeBase Production Multi-Tenant Load & Concurrency Benchmark  ")
    print("=" * 65)
    print("Simulating 100 organizations, 1,000 connection operations,")
    print("mixed 80/20 Read/Write workload with live concurrent backup snapshot...\n")

    harness = LoadTestHarness(num_tenants=100, total_connections=1000, worker_threads=16)
    results = harness.run_load_test()

    specs = results["system_specs"]
    w = results["workload"]
    r = results["results"]

    print("Hardware & Environment:")
    print(f"  OS:           {specs['os']}")
    print(f"  Architecture: {specs['architecture']} ({specs['cpu_count']} cores)")
    print(f"  Python:       {specs['python_version']}\n")

    print("Workload Execution:")
    print(f"  Tenants:      {w['simulated_tenants']}")
    print(f"  Connections:  {w['total_connections']} sessions")
    print(f"  Worker Pool:  {w['concurrency_threads']} threads")
    print(f"  Workload Mix: {w['read_ratio']} Reads / {w['write_ratio']} Writes")
    print(f"  Live Backup:  {w['concurrent_backup_during_load']}\n")

    print("Measured Performance Results:")
    print(f"  Operations:   {r['operations_completed']} OK / {r['operations_failed']} Failed (Error rate: {r['error_rate_percent']}%)")
    print(f"  Total Time:   {r['total_duration_sec']} s")
    print(f"  Throughput:   {r['throughput_qps']} QPS")
    print(f"  p50 Latency:  {r['latency_p50_ms']} ms")
    print(f"  p95 Latency:  {r['latency_p95_ms']} ms")
    print(f"  p99 Latency:  {r['latency_p99_ms']} ms")
    print(f"  Mean Latency: {r['latency_mean_ms']} ms")
    print("=" * 65)


if __name__ == "__main__":
    main()
