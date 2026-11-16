@echo off
setlocal enabledelayedexpansion

:: Navigate to project directory
cd /d "%~dp0"

echo ========================================================
echo  CeeBase PATH Setup Helper
echo ========================================================
echo.

:: Detect executable directory
set "BIN_DIR="
if exist "%~dp0build\ceebase.exe" set "BIN_DIR=%~dp0build"
if exist "%~dp0build\Release\ceebase.exe" set "BIN_DIR=%~dp0build\Release"
if exist "%~dp0build\Debug\ceebase.exe" set "BIN_DIR=%~dp0build\Debug"

if not defined BIN_DIR (
    echo [WARNING] ceebase.exe has not been built yet.
    echo Please run run-ceebase.bat first to build the executable.
    pause
    exit /b 1
)

echo Detected CeeBase directory:
echo   %BIN_DIR%
echo.
echo This script will add the directory above to your User PATH variable.
echo No system-wide or machine-wide settings will be changed.
echo.
set /p USER_CHOICE="Do you want to proceed and add CeeBase to your User PATH? (y/N): "

if /i "%USER_CHOICE%" neq "y" (
    echo Operation cancelled by user. No settings were modified.
    pause
    exit /b 0
)

echo.
echo Adding to User PATH...
for /f "tokens=2*" %%A in ('reg query HKCU\Environment /v PATH 2^>nul') do set "CURRENT_USER_PATH=%%B"

if defined CURRENT_USER_PATH (
    echo !CURRENT_USER_PATH! | findstr /i /c:"%BIN_DIR%" >nul
    if %errorlevel% equ 0 (
        echo [INFO] %BIN_DIR% is already present in your User PATH.
        pause
        exit /b 0
    )
    setx PATH "%CURRENT_USER_PATH%;%BIN_DIR%"
) else (
    setx PATH "%BIN_DIR%"
)

if %errorlevel% equ 0 (
    echo [SUCCESS] CeeBase was successfully added to your User PATH.
    echo Please open a new Command Prompt or PowerShell window to use 'ceebase' directly.
) else (
    echo [ERROR] Failed to update User PATH.
)

pause
