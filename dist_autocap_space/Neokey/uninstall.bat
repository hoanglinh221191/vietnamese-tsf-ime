@echo off
:: Neokey Portable Uninstallation Script Wrapper
:: This script runs register.ps1 -Unregister to cleanly remove the TSF IME in-place.
cd /d "%~dp0"
echo Unregistering Neokey in-place (requires Administrator privileges)...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0register.ps1" -Unregister %*
if %errorlevel% neq 0 (
    echo.
    echo Unregistration failed with exit code %errorlevel%.
) else (
    echo.
    echo Unregistration completed successfully.
)
pause
