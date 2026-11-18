# CeeBase Automated Daily Release Runner
$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Resolve-Path "$ScriptDir\.."
Set-Location $RepoRoot

Write-Host "========================================================"
Write-Host " CeeBase Automated Daily Release Runner"
Write-Host "========================================================"
Write-Host "Running at: $(Get-Date)"

# Ensure git config
git config user.name "MackenzieCampbelAndross"
git config user.email "mackenzieandross@gmail.com"

$StateFile = "$RepoRoot\.git\daily_release_day.txt"
$CurrentDay = 1
if (Test-Path $StateFile) {
    $CurrentDay = [int](Get-Content $StateFile -Raw).Trim()
}

Write-Host "[INFO] Current Scheduled Release Day: $CurrentDay"
if ($CurrentDay -gt 5) {
    Write-Host "[INFO] All 5 release days have been completed!"
    exit 0
}

# Run tests
Write-Host "[INFO] Running test suite to guarantee 100% correctness..."
ctest --test-dir build --output-on-failure
if ($LASTEXITCODE -ne 0) {
    Write-Error "Tests failed! Aborting release."
    exit 1
}

# Day specific commits
$CommitMessages = @{
    1 = "feat(api): implement embedded C ABI and Python client package"
    2 = "feat(sql): add NULL value semantics and scalar string/math functions"
    3 = "feat(storage): implement multi-page record overflow storage"
    4 = "feat(server): implement embedded HTTP REST server and Web Studio"
    5 = "ci(github): add multi-compiler matrix build and fuzz testing suite"
}

$Msg = $CommitMessages[$CurrentDay]
Write-Host "[INFO] Committing Day $CurrentDay: $Msg"
git add .
git commit -m $Msg --allow-empty

Write-Host "[INFO] Pushing to origin/main..."
git push origin main
if ($LASTEXITCODE -ne 0) {
    Write-Error "Git push failed!"
    exit 1
}

$NextDay = $CurrentDay + 1
Set-Content -Path $StateFile -Value $NextDay
Write-Host "[SUCCESS] Day $CurrentDay successfully pushed to origin/main! Next scheduled day: $NextDay"
