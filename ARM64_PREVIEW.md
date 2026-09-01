# Neokey ARM64 Preview

This archive is an experimental native ARM64 build for developers and testers
who have a Windows on ARM device. It is published separately from the supported
`NeokeySetup.exe` and `Neokey-portable.zip` packages.

## Evidence and limitations

- The DLL, configuration app, and regression-test executable were cross-compiled
  with Microsoft Visual C++ for PE machine type `ARM64` (`0xAA64`).
- The ARM64 test executable was compiled but could not be executed on the x64
  release machine.
- Native ARM64 TSF registration, typing behavior, uninstall, and host compatibility
  have not yet been smoke-tested on Windows on ARM.
- This preview is not digitally signed and does not include an automated installer.
- Do not replace files in an existing x64/x86 Neokey installation with these files.

## Files

- `neokey_arm64.dll`: native ARM64 TSF DLL.
- `neokey_config.exe`: native ARM64 configuration application.
- `core_tests_arm64.exe`: native ARM64 regression-test executable.
- `neokey_manifest.json`: SHA-256 hashes and sizes for the preview payload.

## Manual testing on Windows on ARM

Use a separate, stable folder. From a native ARM64 Administrator terminal:

```bat
C:\Windows\System32\regsvr32.exe neokey_arm64.dll
```

Then run `neokey_config.exe`, add/select Neokey in Windows language input
settings if needed, restart the target application, and test both ordinary text
fields and native ARM64 applications. Run the regression suite with:

```bat
core_tests_arm64.exe
```

To unregister the preview from a native ARM64 Administrator terminal:

```bat
C:\Windows\System32\regsvr32.exe /u neokey_arm64.dll
```

## Tiếng Việt

Đây là bản thử nghiệm dành cho máy Windows ARM64. Bản này mới được biên dịch
chéo và xác minh định dạng `AA64`; chưa được kiểm thử cài đặt, gỡ cài đặt hay gõ
thực tế trên máy ARM. Không chép đè các tệp này lên bản x64/x86 đang sử dụng.
Hãy dùng một thư mục riêng và báo rõ phiên bản Windows, ứng dụng thử nghiệm và
kết quả của `core_tests_arm64.exe` khi gửi phản hồi.
