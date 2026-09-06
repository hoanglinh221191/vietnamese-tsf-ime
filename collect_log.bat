@echo off
setlocal
set "OUT=%~dp0logs\neokey_tail.log"
set "SRC=%TEMP%\neokey.log"
if not exist "%SRC%" (
  echo Khong tim thay "%SRC%"
  pause
  exit /b 1
)
echo Dang trich xuat log tu "%SRC%" ...
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$src='%SRC%'; $out='%OUT%'; Get-Content -LiteralPath $src -Tail 40000 -Encoding UTF8 | Set-Content -LiteralPath $out -Encoding UTF8; Write-Host ('Da ghi ' + (Get-Item $out).Length + ' bytes -> ' + $out)"
echo Xong. Bao Claude la da chay xong.
pause
