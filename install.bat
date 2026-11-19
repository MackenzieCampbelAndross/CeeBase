@echo off
setlocal
cd /d "%~dp0"
echo ========================================================
echo  Installing CeeBase CLI...
echo ========================================================
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\install.ps1"
if %errorlevel% neq 0 (
    echo [ERROR] Installation failed.
    pause
    exit /b %errorlevel%
)
