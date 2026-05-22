@echo off
:: Neokey Portable Installation Script Wrapper
:: This script runs register.ps1 with execution bypass to register the TSF IME in-place.
cd /d "%~dp0"
echo Registering Neokey in-place (requires Administrator privileges)...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0register.ps1" -RequireManifest %*
if %errorlevel% neq 0 (
    echo.
    echo Registration failed with exit code %errorlevel%.
) else (
    echo.
    echo Registration completed successfully.
)
pause
