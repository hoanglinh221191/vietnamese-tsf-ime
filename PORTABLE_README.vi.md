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
4. Đóng và mở lại mọi ứng dụng đang chạy để chúng nạp bộ gõ mới.
5. Khởi động lại Windows để dịch vụ nhập liệu được nạp lại đầy đủ.
6. Mở `neokey_config.exe` để chọn Telex, Telex đơn giản, hoặc VNI và tùy chỉnh
   sửa lỗi, gõ tắt, và thiết lập ứng dụng.

Trình cài đặt kiểm tra `neokey_manifest.json` trước khi đăng ký và đặt Neokey
làm bộ gõ mặc định cho tài khoản Windows đang chạy cài đặt.

Dữ liệu gõ tắt được lưu riêng trong `%LOCALAPPDATA%\Neokey` và tự di chuyển từ
file portable cũ ở lần cài đầu tiên, nên thay gói portable không làm mất dữ liệu.

## Gõ tắt động

Trong **Bảng Từ Gõ Tắt**, mỗi dòng vẫn có dạng `phím=nội dung`; các quy tắc tĩnh
cũ tiếp tục hoạt động. Nội dung có thể dùng các biến sau:

- `{{DD/MM/YYYY}}`: ngày hiện tại theo giờ địa phương, ví dụ `30/08/2026`.
- `{{DATE}}`: tên ngắn tương đương `{{DD/MM/YYYY}}`.
- `{{TIME}}`: giờ địa phương dạng 24 giờ `HH:mm`.
- `{{WEEKDAY}}`: thứ hiện tại bằng tiếng Việt.
- `{{UUID}}`: UUID mới, không có dấu ngoặc, ví dụ
  `12345678-1234-4abc-8def-1234567890ab`.
- `{{NEWLINE}}` và `{{TAB}}`: chèn xuống dòng Windows hoặc ký tự tab.
- `{{CLIPBOARD}}`: văn bản Unicode hiện có trong clipboard, giữ nguyên chữ hoa,
  chữ thường và xuống dòng.
- `{{CLIPBOARD|TRIM}}`, `{{CLIPBOARD|UPPER}}` và
  `{{CLIPBOARD|LOWER}}`: bỏ khoảng trắng ở hai đầu, đổi thành chữ hoa hoặc chữ
  thường. Nội dung clipboard gốc không bị sửa.
- `{{SELECTION}}`: văn bản đang được chọn trước khi gõ phím đầu tiên của từ gõ
  tắt. Hãy gõ từ tắt ngay khi vùng chọn còn hoạt động; đặt caret ở chỗ khác sẽ
  hủy vùng chọn và Neokey không dùng lại nội dung cũ.
- `{{CURSOR}}`: bỏ tag và đưa caret về đúng vị trí đó sau khi hoàn tất mở rộng.
  Mỗi quy tắc được dùng tối đa một tag `{{CURSOR}}`.

Ví dụ:

```text
eml=email_cua_toi@gmail.com
dday=Hôm nay là ngày {{DD/MM/YYYY}}
xchao=Kính gửi anh/chị {{CLIPBOARD}},
stamp={{DATE}} {{TIME}} - {{UUID}}
wrap=[{{SELECTION}}]{{CURSOR}}
clip={{CLIPBOARD|TRIM}}
```

Neokey chỉ đọc clipboard khi quy tắc được kích hoạt và không ghi nội dung đó
vào log. Nếu clipboard đang trống, bị khóa, không chứa văn bản Unicode, hoặc
kết quả vượt giới hạn 16.384 ký tự, Neokey giữ nguyên phím gõ tắt thay vì chèn
một kết quả thiếu. `{{SELECTION}}` cũng thất bại an toàn nếu ứng dụng không cho
đọc vùng chọn. Với `{{CURSOR}}`, Neokey chỉ áp dụng khi có thể xác minh cả thay
văn bản lẫn caret; nếu lỗi, văn bản được rollback và phím kích hoạt vẫn chỉ
được xử lý một lần. Để giữ nguyên state-machine công thức hiện đã xác nhận,
`{{SELECTION}}` và `{{CURSOR}}` tạm thời thất bại an toàn trong Excel; các biến
chỉ tạo văn bản vẫn hoạt động. Biến không được hỗ trợ được giữ nguyên dưới dạng
văn bản.
Sau khi lưu hoặc sửa trực tiếp file gõ tắt, bảng mới được kiểm tra tại lần mở
rộng kế tiếp; không cần đóng và mở lại ứng dụng đang gõ.

Việc mở lại ứng dụng là cần thiết vì ứng dụng đang chạy có thể vẫn giữ TSF DLL
cũ. Nếu Neokey chưa xuất hiện hoặc ứng dụng vẫn dùng bản cũ, hãy khởi động lại
Windows trước khi kiểm tra thêm.

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
4. Đóng và mở lại các ứng dụng đang chạy, sau đó khởi động lại Windows để nạp
   bộ gõ mới.

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

Neokey được phát hành theo giấy phép MIT. Xem `LICENSE`. Dữ liệu từ điển song
ngữ tiếng Anh đi kèm có thông báo ghi công riêng trong `THIRD_PARTY_NOTICES.md`.
