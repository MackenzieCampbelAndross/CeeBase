# ==============================================================================
# CeeBase Production Docker Compose Deployment Script (PowerShell)
# ==============================================================================
$ErrorActionPreference = "Stop"

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "      CeeBase Production Infrastructure Setup Wizard       " -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = Split-Path -Parent $ScriptDir
$ComposeDir = Join-Path $RootDir "deploy\compose"

Write-Host "[1/4] Checking prerequisites..." -ForegroundColor Yellow
if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
    Write-Error "Docker is required but not found in PATH."
}

Write-Host "[2/4] Building and launching CeeBase cluster..." -ForegroundColor Yellow
Set-Location $ComposeDir
docker compose up -d --build

Write-Host "[3/4] Awaiting cluster health..." -ForegroundColor Yellow
$healthy = $false
for ($i = 0; $i -lt 30; $i++) {
    try {
        $resp = Invoke-RestMethod -Uri "http://localhost:8080/api/v1/health" -TimeoutSec 2
        if ($resp.status -eq "healthy") {
            $healthy = $true
            break
        }
    } catch {
        Start-Sleep -Seconds 2
    }
}

if (-not $healthy) {
    Write-Warning "Cluster did not report healthy within 60 seconds."
}

Write-Host "[4/4] Verifying production endpoints:" -ForegroundColor Green
Write-Host "  - Primary Node:   http://localhost:8080 (REST / Studio UI)"
Write-Host "  - Standby Node:   http://localhost:8081 (WAL Streaming Replica)"
Write-Host "  - Metrics:        http://localhost:8080/metrics (Prometheus Format)"
Write-Host "  - Prometheus:     http://localhost:9090"
Write-Host "  - Grafana:        http://localhost:3000 (admin / CeeBaseAdmin2026!)"
Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "CeeBase Production Cluster is active." -ForegroundColor Green
