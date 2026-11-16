@echo off
setlocal enabledelayedexpansion

:: Navigate to project directory
cd /d "%~dp0"

echo ========================================================
echo  CeeBase Windows Launcher
echo ========================================================

:: Detect executable location
set "CEEBASE_EXE="
if exist "build\ceebase.exe" set "CEEBASE_EXE=build\ceebase.exe"
if exist "build\Release\ceebase.exe" set "CEEBASE_EXE=build\Release\ceebase.exe"
if exist "build\Debug\ceebase.exe" set "CEEBASE_EXE=build\Debug\ceebase.exe"

if defined CEEBASE_EXE (
    goto :LAUNCH
)

echo [INFO] CeeBase executable not found. Starting build...

:: Check CMake availability
where cmake >nul 2>nul
if %errorlevel% neq 0 (
    echo [ERROR] CMake was not found in PATH.
    echo Please install CMake and ensure it is available in your PATH environment variable.
    pause
    exit /b 1
)

:: Configure build
if not exist "build" (
    echo [INFO] Configuring CMake build...
    cmake -S . -B build
    if %errorlevel% neq 0 (
        echo [ERROR] CMake configuration failed.
        pause
        exit /b 1
    )
)

:: Build CeeBase
echo [INFO] Compiling CeeBase target...
cmake --build build --config Release --target ceebase
if %errorlevel% neq 0 (
    echo [ERROR] Build failed! Check the compiler errors above.
    pause
    exit /b 1
)

:: Re-check executable location
if exist "build\ceebase.exe" set "CEEBASE_EXE=build\ceebase.exe"
if exist "build\Release\ceebase.exe" set "CEEBASE_EXE=build\Release\ceebase.exe"
if exist "build\Debug\ceebase.exe" set "CEEBASE_EXE=build\Debug\ceebase.exe"

if not defined CEEBASE_EXE (
    echo [ERROR] Build completed but ceebase.exe was not found in expected directories.
    pause
    exit /b 1
)

:LAUNCH
echo [INFO] Launching %CEEBASE_EXE%...
echo.
"%CEEBASE_EXE%" %*
set "EXIT_CODE=%errorlevel%"

exit /b %EXIT_CODE%
