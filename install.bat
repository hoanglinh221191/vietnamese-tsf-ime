@echo off
setlocal EnableExtensions DisableDelayedExpansion

:: Portable fallback installer. NeokeySetup.exe is recommended for most users.
:: This script registers the DLLs in place and makes Neokey the default input
:: method for the Windows account that launched it.
cd /d "%~dp0"

set "NEOKEY_VERSION=unknown"
if exist "%~dp0VERSION" set /p "NEOKEY_VERSION="<"%~dp0VERSION"

set "POWERSHELL=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if not exist "%POWERSHELL%" (
    echo.
    echo Neokey %NEOKEY_VERSION% could not be installed.
    echo Windows PowerShell was not found.
    echo.
    pause
    exit /b 1
)

echo.
echo ========================================
echo   Installing Neokey %NEOKEY_VERSION%
echo ========================================
echo.
echo Keep this folder in its current location after installation.
echo Windows will ask for Administrator permission once.
echo.

"%POWERSHELL%" -NoProfile -ExecutionPolicy Bypass -File "%~dp0register.ps1" -VerifyManifest
if errorlevel 1 (
    set "INSTALL_EXIT=1"
    goto :failed
)

"%POWERSHELL%" -NoProfile -ExecutionPolicy Bypass -File "%~dp0register.ps1" -RequireManifest -SetDefault %*
set "INSTALL_EXIT=%ERRORLEVEL%"
if not "%INSTALL_EXIT%"=="0" goto :failed

echo.
echo Neokey %NEOKEY_VERSION% was installed successfully.
echo It is now the default input method for this Windows account.
echo Reopen any apps that were already running before installation.
echo.
echo Press any key to close this window.
pause >nul
exit /b 0

:failed
echo.
echo Neokey installation did not complete.
echo If you cancelled the Administrator prompt, run install.bat again.
echo Error code: %INSTALL_EXIT%
echo.
echo Press any key to close this window.
pause >nul
exit /b %INSTALL_EXIT%
