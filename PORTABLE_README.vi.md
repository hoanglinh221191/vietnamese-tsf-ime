# Neokey Portable

[English](README.md)

Thư mục này là bản phát hành portable của Neokey cho Windows. Thư mục chứa các
TSF DLL 64-bit và 32-bit, ứng dụng cấu hình, và script cần thiết để đăng ký bộ
gõ ngay tại vị trí hiện tại.

Đây là lựa chọn dành cho người dùng cần bản portable. Với cài đặt thông thường,
hãy dùng `NeokeySetup.exe` từ trang GitHub Releases để có cập nhật tại chỗ và
gỡ cài đặt qua Windows Settings.

## Cài đặt

1. Giữ toàn bộ thư mục này tại một vị trí ổn định, ví dụ `C:\Neokey`. Không di
   chuyển thư mục sau khi cài vì Windows lưu đường dẫn DLL.
2. Nhấn đúp `install.bat`.
3. Chấp nhận yêu cầu quyền Quản trị viên của Windows khi xuất hiện.
4. Mở `neokey_config.exe` để chọn Telex, Telex đơn giản, hoặc VNI và tùy chỉnh
   sửa lỗi, gõ tắt, và thiết lập ứng dụng.

Trình cài đặt kiểm tra `neokey_manifest.json` trước khi đăng ký và đặt Neokey
làm bộ gõ mặc định cho tài khoản Windows đang chạy cài đặt.

Dữ liệu gõ tắt được lưu riêng trong `%LOCALAPPDATA%\Neokey` và tự di chuyển từ
file portable cũ ở lần cài đầu tiên, nên thay gói portable không làm mất dữ liệu.

Nếu bộ gõ chưa xuất hiện ngay, hãy chuyển bộ gõ một lần hoặc đăng xuất và đăng
nhập lại Windows.

## Về `neokey_config.exe`

`neokey_config.exe` là ứng dụng cấu hình và khay hệ thống riêng biệt, không
phải bộ máy gõ tiếng Việt. Việc gõ được các TSF DLL đã đăng ký thực hiện, nên
bạn không cần giữ cửa sổ cấu hình mở để gõ tiếng Việt. Hãy chạy ứng dụng này
khi cần thay đổi kiểu gõ, mức sửa lỗi, gõ tắt, khởi động cùng Windows, hoặc
danh sách ứng dụng chặn.

## Kiểm tra cài đặt

Mở PowerShell trong thư mục này và chạy:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\register.ps1 -Status
```

Kết quả cần cho biết DLL 64-bit và 32-bit đã được đăng ký và hash manifest
khớp nhau.

Để chỉ kiểm tra các tệp phát hành trước khi cài đặt:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\register.ps1 -VerifyManifest
```

## Cập nhật

1. Chạy `uninstall.bat` trong thư mục cũ.
2. Giải nén thư mục `Neokey` mới vào vị trí ổn định.
3. Chạy `install.bat` của bản mới.

Không sửa hoặc thay thế `neokey.dll`, `neokey32.dll`, hoặc
`neokey_config.exe`. Chúng được bảo vệ bởi release manifest. Hãy tạo gói mới
nếu cần build phiên bản khác.

## Gỡ cài đặt

Chạy `uninstall.bat` trong thư mục này và chấp nhận yêu cầu quyền Quản trị
viên. Sau đó có thể xóa thư mục.

## Câu hỏi thường gặp

**Neokey không gõ được tiếng Việt trong một ứng dụng**

Mở `neokey_config.exe`, đảm bảo đã chọn chế độ tiếng Việt, và kiểm tra xem ứng
dụng có nằm trong danh sách chặn hay không.

**Neokey không hoạt động trong Windows Terminal hoặc Command Prompt**

Terminal mặc định được bỏ qua để giữ nguyên chỉnh sửa lệnh và gợi ý. Hãy xóa
ứng dụng tương ứng khỏi danh sách chặn trong `neokey_config.exe` nếu bạn muốn
bật Neokey trong đó.

**Tôi đã di chuyển thư mục sau khi cài đặt**

Nếu có thể, hãy chuyển nó về vị trí cũ. Nếu không, chạy `uninstall.bat`, đặt
thư mục vào vị trí mới cố định, rồi chạy lại `install.bat`.

## Giấy phép

Neokey được phát hành theo giấy phép MIT. Xem `LICENSE`.
