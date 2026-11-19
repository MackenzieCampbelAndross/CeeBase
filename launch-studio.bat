@echo off
setlocal

:: Navigate to project root
cd /d "%~dp0"

echo ========================================================
echo  CeeBase Studio: Visual Web Management Dashboard
echo ========================================================
echo.

:: Check python availability
where python >nul 2>nul
if %errorlevel% neq 0 (
    echo [ERROR] Python was not found in PATH.
    echo Please ensure Python 3.8+ is installed to run CeeBase Studio.
    pause
    exit /b 1
)

:: Run the Studio web server
python tools/studio/server.py ceebase.cbdb

pause
