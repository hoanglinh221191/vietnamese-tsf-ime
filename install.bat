@echo off
:: Neokey Portable Installation Script Wrapper
:: Run normally; register.ps1 requests elevation only for system-wide DLL registration.
:: Pass -SetDefault to make Neokey the current user's default input method.
cd /d "%~dp0"
if exist "%~dp0VERSION" (
    set /p NEOKEY_VERSION=<"%~dp0VERSION"
    echo Neokey version %NEOKEY_VERSION%
)
echo Registering Neokey in-place (requires Administrator privileges)...
echo Use -SetDefault to keep Neokey selected after sign-in or reboot.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0register.ps1" -RequireManifest %*
if %errorlevel% neq 0 (
    echo.
    echo Registration failed with exit code %errorlevel%.
) else (
    echo.
    echo Registration completed successfully.
)
pause
