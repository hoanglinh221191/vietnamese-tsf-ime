# Neokey

[English](README.en.md)

Neokey là bộ gõ tiếng Việt mã nguồn mở cho Windows. Bộ gõ sử dụng Windows Text
Services Framework (TSF) và hỗ trợ các kiểu gõ Telex, Telex đơn giản, và VNI
trong một IME Windows native.

## Tính năng nổi bật

- Bộ gõ TSF native cho ứng dụng desktop Windows 64-bit và 32-bit.
- Hỗ trợ Telex, Telex đơn giản, và VNI.
- Các mức sửa lỗi có thể tùy chỉnh: Tắt, Thường, Nâng cao, và Thử nghiệm.
- Bảo vệ tiếng Anh/mã nguồn, gõ tắt, và điều khiển bộ gõ theo từng ứng dụng.
- Tự động tạm tắt chuyển đổi trong ô mật khẩu để không can thiệp vào dữ liệu
  nhạy cảm.
- Danh sách loại trừ ứng dụng, kèm tùy chọn tự động loại trừ ứng dụng khi
  chuyển sang tiếng Anh và tự khôi phục khi trở lại tiếng Việt.
- Ứng dụng cấu hình và khay hệ thống nhẹ (`neokey_config.exe`).
- Gói portable kèm manifest SHA-256 để kiểm tra các tệp nhị phân phát hành.

## Cài bản portable

Cách dễ nhất để dùng Neokey là tải gói portable từ trang GitHub Releases.

1. Tải `Neokey-portable.zip` và giải nén thư mục `Neokey` vào một vị trí ổn
   định, ví dụ `C:\Neokey`.
2. Không di chuyển thư mục sau khi cài. Windows lưu đường dẫn tuyệt đối của DLL
   khi đăng ký bộ gõ.
3. Chạy `install.bat` và chấp nhận yêu cầu quyền Quản trị viên của Windows.
4. Mở `neokey_config.exe` để chọn Telex, Telex đơn giản, hoặc VNI và tùy chỉnh
   sửa lỗi, gõ tắt, và thiết lập ứng dụng.

`install.bat` kiểm tra manifest trước khi đăng ký và đặt Neokey làm bộ gõ mặc
định cho tài khoản Windows đang chạy cài đặt.

Hướng dẫn portable đầy đủ bằng tiếng Việt nằm tại
[PORTABLE_README.vi.md](PORTABLE_README.vi.md). Bản tiếng Anh nằm tại
[PORTABLE_README.md](PORTABLE_README.md).

## Bảo vệ mật khẩu và điều khiển ứng dụng

Khi Windows đánh dấu ô nhập hiện tại là mật khẩu, Neokey tự bỏ qua chuyển đổi
tiếng Việt để nội dung nhạy cảm được nhập nguyên trạng.

Trong `neokey_config.exe`, bạn có thể chặn Neokey trong từng ứng dụng bằng tên
tiến trình. Tùy chọn **Tự động loại trừ app khi chuyển sang tiếng Anh** sẽ tạm
thêm ứng dụng đang dùng vào danh sách chặn khi bạn chuyển sang tiếng Anh, rồi
chỉ gỡ mục do tính năng này thêm khi quay lại tiếng Việt. Những ứng dụng bạn tự
chặn vẫn được giữ nguyên.

## Kiểm tra hoặc gỡ cài đặt

Từ thư mục portable, kiểm tra trạng thái cài đặt bằng lệnh:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\register.ps1 -Status
```

Để gỡ Neokey, chạy `uninstall.bat` trong đúng thư mục đã dùng để cài đặt.

## Build từ mã nguồn

Bản build phát hành cần Microsoft Visual C++ Build Tools và Windows SDK. Từ
thư mục gốc của repository, chạy:

```bat
build.bat
build\cxx23\core_tests.exe
package.bat -Zip
```

Thư mục portable được tạo tại `dist\Neokey`; tệp nén tùy chọn được tạo tại
`dist\Neokey-portable.zip`. Hãy chỉ phân phối thư mục hoàn chỉnh hoặc tệp nén,
không sao chép riêng lẻ tệp từ `build`.

## Về `neokey_config.exe`

`neokey_config.exe` là ứng dụng cấu hình và khay hệ thống riêng biệt. Bộ máy
gõ thực sự là các TSF DLL được `install.bat` đăng ký, vì vậy Neokey vẫn gõ được
khi cửa sổ cấu hình đã đóng. Hãy mở ứng dụng này khi cần thay đổi kiểu gõ, mức
sửa lỗi, gõ tắt, khởi động cùng Windows, hoặc danh sách ứng dụng chặn.

## Lưu ý cho terminal

Neokey mặc định bỏ qua các terminal thông dụng để giữ nguyên việc nhập lệnh,
gợi ý, và trình soạn thảo terminal. Để bật Neokey trong terminal, dùng
`neokey_config.exe` để sửa danh sách ứng dụng chặn.

## Giấy phép

Neokey được phát hành theo [giấy phép MIT](LICENSE).
