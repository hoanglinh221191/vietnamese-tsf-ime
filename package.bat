@echo off
:: Builds, tests, and creates a clean portable Neokey release folder.
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0package.ps1" %*
if %errorlevel% neq 0 (
    echo.
    echo Packaging failed with exit code %errorlevel%.
    exit /b %errorlevel%
)
echo.
echo Packaging completed successfully.
