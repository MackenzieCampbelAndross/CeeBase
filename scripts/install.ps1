# CeeBase CLI Installer for Windows
# Usage (Online):
#   irm https://raw.githubusercontent.com/MackenzieCampbelAndross/CeeBase/main/scripts/install.ps1 | iex
# Usage (Local):
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\install.ps1

$ErrorActionPreference = "Stop"

Write-Host "========================================================" -ForegroundColor Cyan
Write-Host "  CeeBase CLI Installer (Windows)" -ForegroundColor Cyan
Write-Host "========================================================" -ForegroundColor Cyan
Write-Host ""

$InstallDir = Join-Path $HOME ".ceebase\bin"
$ExePath = Join-Path $InstallDir "ceebase.exe"

if (-not (Test-Path $InstallDir)) {
    New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
}

$Installed = $false

# 1. Check if running from local repository
$ScriptDir = if ($PSScriptRoot) { $PSScriptRoot } else { $PWD.Path }
$RepoCandidates = @(
    "tools\cli\target\release\ceebase.exe",
    "build\ceebase.exe",
    (Join-Path $ScriptDir "..\tools\cli\target\release\ceebase.exe"),
    (Join-Path $ScriptDir "..\build\ceebase.exe")
)

foreach ($Cand in $RepoCandidates) {
    if ($Cand -and (Test-Path $Cand)) {
        Write-Host "[+] Installing from local build: $Cand" -ForegroundColor Green
        Copy-Item -Path $Cand -Destination $ExePath -Force
        $LocalDll = Join-Path (Split-Path -Parent $Cand) "libceebase.dll"
        if (Test-Path $LocalDll) {
            Copy-Item -Path $LocalDll -Destination (Join-Path $InstallDir "libceebase.dll") -Force -ErrorAction SilentlyContinue
        }
        $Installed = $true
        break
    }
}

# 2. If no local build, attempt download from GitHub Releases
if (-not $Installed) {
    Write-Host "[+] Fetching latest CeeBase release from GitHub..." -ForegroundColor Yellow
    $Repo = "MackenzieCampbelAndross/CeeBase"
    $ApiUrl = "https://api.github.com/repos/$Repo/releases/latest"

    try {
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        $Release = Invoke-RestMethod -Uri $ApiUrl -Headers @{ "User-Agent" = "CeeBase-Installer" }
        $Asset = $Release.assets | Where-Object { $_.name -like "*windows*.zip" -or $_.name -like "*windows*.exe" } | Select-Object -First 1

        if ($Asset) {
            Write-Host "[+] Downloading $($Asset.name)..." -ForegroundColor Yellow
            $TempFile = Join-Path $env:TEMP $Asset.name
            Invoke-WebRequest -Uri $Asset.browser_download_url -OutFile $TempFile -UseBasicParsing

            if ($Asset.name.EndsWith(".zip")) {
                Expand-Archive -Path $TempFile -DestinationPath $InstallDir -Force
                Remove-Item $TempFile -Force
            } else {
                Move-Item -Path $TempFile -Destination $ExePath -Force
            }
            $Installed = $true
        }
    } catch {
        Write-Host "[-] Note: Remote release asset not found; trying fallback build." -ForegroundColor Gray
    }
}

# 3. If cargo is available and tools/cli exists, compile
if (-not $Installed -and (Get-Command cargo -ErrorAction SilentlyContinue)) {
    if (Test-Path "tools\cli\Cargo.toml") {
        Write-Host "[+] Compiling CeeBase CLI with Cargo..." -ForegroundColor Yellow
        cargo build --release --manifest-path tools\cli\Cargo.toml
        Copy-Item "tools\cli\target\release\ceebase.exe" $ExePath -Force
        $Installed = $true
    }
}

if (-not (Test-Path $ExePath)) {
    Write-Host "[ERROR] Could not install ceebase.exe." -ForegroundColor Red
    exit 1
}

# 4. Copy to directories already in standard PATH for immediate execution
$DllPath = Join-Path $InstallDir "libceebase.dll"

$CargoBin = Join-Path $HOME ".cargo\bin"
if (Test-Path $CargoBin) {
    Copy-Item -Path $ExePath -Destination (Join-Path $CargoBin "ceebase.exe") -Force -ErrorAction SilentlyContinue
    if (Test-Path $DllPath) {
        Copy-Item -Path $DllPath -Destination (Join-Path $CargoBin "libceebase.dll") -Force -ErrorAction SilentlyContinue
    }
}

$WindowsApps = Join-Path $env:LOCALAPPDATA "Microsoft\WindowsApps"
if (Test-Path $WindowsApps) {
    Copy-Item -Path $ExePath -Destination (Join-Path $WindowsApps "ceebase.exe") -Force -ErrorAction SilentlyContinue
    if (Test-Path $DllPath) {
        Copy-Item -Path $DllPath -Destination (Join-Path $WindowsApps "libceebase.dll") -Force -ErrorAction SilentlyContinue
    }
}

# 5. Add to User PATH in Registry (Permanent across all new terminals)
$UserPath = [Environment]::GetEnvironmentVariable("Path", "User")
$PathArray = $UserPath -split ";" | Where-Object { $_ -ne "" }

if ($PathArray -notcontains $InstallDir) {
    Write-Host "[+] Adding $InstallDir to User PATH..." -ForegroundColor Green
    $NewUserPath = if ([string]::IsNullOrEmpty($UserPath)) { $InstallDir } else { "$UserPath;$InstallDir" }
    [Environment]::SetEnvironmentVariable("Path", $NewUserPath, "User")
}

# Update current session PATH so 'ceebase' works immediately
if ($env:PATH -split ";" -notcontains $InstallDir) {
    $env:PATH = "$InstallDir;$env:PATH"
}

Write-Host ""
Write-Host "========================================================" -ForegroundColor Green
Write-Host "  CeeBase CLI is successfully installed!" -ForegroundColor Green
Write-Host "========================================================" -ForegroundColor Green
Write-Host ""
Write-Host "Installed to: $ExePath" -ForegroundColor White
& $ExePath --version
Write-Host ""
Write-Host "To start CeeBase, open any terminal and type:" -ForegroundColor Cyan
Write-Host "  ceebase" -ForegroundColor Yellow
Write-Host ""
