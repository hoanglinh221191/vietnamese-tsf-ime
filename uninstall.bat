@echo off
setlocal EnableExtensions DisableDelayedExpansion

:: Portable fallback uninstaller. Run it from the same folder used to install.
cd /d "%~dp0"

set "NEOKEY_VERSION=unknown"
if exist "%~dp0VERSION" set /p "NEOKEY_VERSION="<"%~dp0VERSION"

set "POWERSHELL=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if not exist "%POWERSHELL%" (
    echo Windows PowerShell was not found.
    pause
    exit /b 1
)

echo.
echo Uninstalling Neokey %NEOKEY_VERSION%...
echo.
echo This removes Neokey completely:
echo   - the input service registration, for every user on this machine
echo   - your settings, including app rules and shorthand entries
echo   - the log file
echo.
echo Pass -KeepUserData to keep the shorthand file you typed.
echo Windows will ask for Administrator permission once.
echo.

"%POWERSHELL%" -NoProfile -ExecutionPolicy Bypass -File "%~dp0register.ps1" -Unregister %*
set "UNINSTALL_EXIT=%ERRORLEVEL%"
if not "%UNINSTALL_EXIT%"=="0" goto :failed

echo.
echo Neokey was removed successfully.
echo.
echo Close and reopen your applications: each one loaded the input service when
echo it started, and keeps it until it is restarted.
echo You can now delete this portable folder.
echo.
echo Press any key to close this window.
pause >nul
exit /b 0

:failed
echo.
echo Neokey could not be removed completely.
echo Error code: %UNINSTALL_EXIT%
echo.
echo Press any key to close this window.
pause >nul
exit /b %UNINSTALL_EXIT%
