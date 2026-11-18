@echo off
setlocal

:: Forward to PowerShell runner with bypass policy
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scheduled-push.ps1"
exit /b %errorlevel%
