@echo off
chcp 65001 >nul
setlocal EnableExtensions DisableDelayedExpansion

:: Trình gỡ cài đặt dự phòng cho bản portable. Hãy chạy từ đúng thư mục đã
:: dùng để cài. Gỡ bằng mục "Gỡ cài đặt Neokey..." trong menu khay cũng được.
cd /d "%~dp0"

set "NEOKEY_VERSION=unknown"
if exist "%~dp0VERSION" set /p "NEOKEY_VERSION="<"%~dp0VERSION"

set "POWERSHELL=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
:: Started from a 32-bit program, cmd is 32-bit and System32 is really
:: SysWOW64: that PowerShell sees the 32-bit half of the registry. Sysnative
:: exists only then, and leads to the 64-bit one.
if exist "%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe" set "POWERSHELL=%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe"
if not exist "%POWERSHELL%" (
    echo Không tìm thấy Windows PowerShell.
    echo Hãy gỡ Neokey bằng mục "Gỡ cài đặt Neokey..." trong menu khay.
    pause
    exit /b 1
)
if not exist "%~dp0register.ps1" (
    echo Thư mục này thiếu register.ps1. Hãy chạy uninstall.bat trong thư mục
    echo đã dùng để cài Neokey, hoặc giải nén lại đúng file zip Neokey đó rồi
    echo chạy uninstall.bat trong thư mục vừa giải nén.
    pause
    exit /b 1
)

:: A window still on Raster Fonts draws the Vietnamese below without its
:: marks. This moves this window alone to a font that has them.
"%POWERSHELL%" -NoProfile -ExecutionPolicy Bypass -File "%~dp0register.ps1" -ReadableConsoleFont

echo.
echo ========================================
echo   Đang gỡ cài đặt Neokey %NEOKEY_VERSION%
echo ========================================
echo.
echo Neokey sẽ được gỡ hoàn toàn:
echo   - bộ gõ được hủy đăng ký cho mọi tài khoản trên máy này
echo   - các cài đặt, gồm cả thiết lập theo ứng dụng và bảng gõ tắt
echo   - file log
echo.
echo Muốn giữ bảng gõ tắt, hãy chạy: uninstall.bat -KeepUserData
echo Windows sẽ yêu cầu quyền Quản trị viên một lần.
echo.

"%POWERSHELL%" -NoProfile -ExecutionPolicy Bypass -File "%~dp0register.ps1" -Unregister %*
set "UNINSTALL_EXIT=%ERRORLEVEL%"
if not "%UNINSTALL_EXIT%"=="0" goto :failed

echo.
echo ========================================================
echo   Đã gỡ Neokey %NEOKEY_VERSION% thành công.
echo ========================================================
echo.
echo Hãy đóng và mở lại các ứng dụng đang chạy: mỗi ứng dụng vẫn giữ bộ gõ
echo đến khi được khởi động lại.
echo Giờ có thể xóa thư mục portable này.
echo.
echo Nhấn phím bất kỳ để đóng cửa sổ này.
pause >nul
exit /b 0

:failed
echo.
echo Chưa gỡ được Neokey hoàn toàn.
echo Mã lỗi: %UNINSTALL_EXIT%
echo.
echo Nếu đã hủy yêu cầu quyền Quản trị viên, hãy chạy lại uninstall.bat.
echo Nếu không, hãy gửi các dòng phía trên: chúng cho biết bước nào bị lỗi.
echo.
echo Nhấn phím bất kỳ để đóng cửa sổ này.
pause >nul
exit /b %UNINSTALL_EXIT%
