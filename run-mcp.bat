@echo off
setlocal

:: Navigate to repository root
cd /d "%~dp0"

python mcp/server.py ceebase.cbdb
