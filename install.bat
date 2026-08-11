@echo off
chcp 65001 >nul
setlocal EnableExtensions DisableDelayedExpansion

:: Trình cài đặt dự phòng cho bản portable. Phần lớn người dùng nên dùng
:: NeokeySetup.exe. Script này đăng ký DLL tại chỗ và đặt Neokey làm bộ gõ
:: mặc định cho tài khoản Windows đã chạy script.
cd /d "%~dp0"

set "NEOKEY_VERSION=unknown"
if exist "%~dp0VERSION" set /p "NEOKEY_VERSION="<"%~dp0VERSION"

set "POWERSHELL=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if not exist "%POWERSHELL%" (
    echo.
    echo Không thể cài đặt Neokey %NEOKEY_VERSION%.
    echo Không tìm thấy Windows PowerShell.
    echo.
    echo Nhấn phím bất kỳ để đóng cửa sổ này.
    pause >nul
    exit /b 1
)

echo.
echo ========================================
echo   Đang cài đặt Neokey %NEOKEY_VERSION%
echo ========================================
echo.
echo Hãy giữ thư mục này ở nguyên vị trí sau khi cài đặt.
echo Windows sẽ yêu cầu quyền Quản trị viên một lần.
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
echo Đã cài đặt Neokey %NEOKEY_VERSION% thành công.
echo Neokey hiện là bộ gõ mặc định của tài khoản Windows này.
echo.
echo QUAN TRỌNG: Hãy đóng và mở lại mọi ứng dụng đang chạy để chúng nạp bộ gõ mới.
echo Hãy khởi động lại Windows sau khi cài đặt hoặc cập nhật Neokey để bảo đảm
echo dịch vụ nhập liệu được nạp lại đầy đủ.
echo.
echo Nhấn phím bất kỳ để đóng cửa sổ này.
pause >nul
exit /b 0

:failed
echo.
echo Quá trình cài đặt Neokey chưa hoàn tất.
echo Nếu bạn đã hủy yêu cầu quyền Quản trị viên, hãy chạy lại install.bat.
echo Mã lỗi: %INSTALL_EXIT%
echo.
echo Nhấn phím bất kỳ để đóng cửa sổ này.
pause >nul
exit /b %INSTALL_EXIT%
