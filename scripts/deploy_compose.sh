#!/usr/bin/env bash
# ==============================================================================
# CeeBase Production Docker Compose Deployment Script
# Guided installation and automated verification
# ==============================================================================
set -euo pipefail

echo "=========================================================="
echo "      CeeBase Production Infrastructure Setup Wizard       "
echo "=========================================================="

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
COMPOSE_DIR="$ROOT_DIR/deploy/compose"

echo "[1/4] Checking prerequisites..."
command -v docker >/dev/null 2>&1 || { echo "Error: docker is required but not installed."; exit 1; }
command -v docker-compose >/dev/null 2>&1 || docker compose version >/dev/null 2>&1 || { echo "Error: docker-compose is required."; exit 1; }

echo "[2/4] Building and launching CeeBase cluster..."
cd "$COMPOSE_DIR"
docker compose up -d --build

echo "[3/4] Awaiting cluster health..."
until curl -sSf http://localhost:8080/api/v1/health >/dev/null 2>&1; do
    echo "  Waiting for Primary database node..."
    sleep 2
done

echo "[4/4] Verifying production endpoints:"
echo "  - Primary Node:   http://localhost:8080 (REST / Studio UI)"
echo "  - Standby Node:   http://localhost:8081 (WAL Streaming Replica)"
echo "  - Metrics:        http://localhost:8080/metrics (Prometheus Format)"
echo "  - Prometheus:     http://localhost:9090"
echo "  - Grafana:        http://localhost:3000 (admin / CeeBaseAdmin2026!)"
echo "=========================================================="
echo "CeeBase Production Cluster is active."
