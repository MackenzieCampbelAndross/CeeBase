@echo off
setlocal

:: Navigate to script directory
cd /d "%~dp0"

echo ========================================================
echo  CeeBase Windows Task Scheduler Setup
echo ========================================================
echo.
echo This utility configures a Windows Task Scheduler task named "CeeBaseDailyRelease"
echo to automatically run scheduled-push.bat once daily at 10:00 AM for the next 5 days.
echo.

set "TASK_NAME=CeeBaseDailyRelease"
set "SCRIPT_PATH=%~dp0scheduled-push.bat"

schtasks /create /tn "%TASK_NAME%" /tr "\"%SCRIPT_PATH%\"" /sc daily /st 10:00 /f

if %errorlevel% equ 0 (
    echo.
    echo [SUCCESS] Windows Scheduled Task '%TASK_NAME%' created successfully!
    echo It will execute automatically every day at 10:00 AM.
    echo.
    echo To run it immediately for today's milestone, you can execute:
    echo   scripts\scheduled-push.bat
) else (
    echo.
    echo [WARNING] Could not create scheduled task automatically (may require Administrator permissions).
    echo You can run the release manually each day by running:
    echo   scripts\scheduled-push.bat
)

pause
