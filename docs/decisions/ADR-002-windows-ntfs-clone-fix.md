# ADR-002: Sửa lỗi không thể Git Clone trên Windows do ký tự không hợp lệ trong tên tệp

## Trạng thái (Status)
**Đã chấp thuận & Áp dụng** (Accepted)  
- **Sơ đồ kiến trúc tương tác (Archify)**: [Xem sơ đồ trực quan (HTML)](./diagrams/ADR-002-windows-ntfs-clone-fix.html)

---

## 1. Sửa, ghi thêm, cập nhật những gì?

1. **Đổi tên tệp mã nguồn Swift**:
   - Đổi tên tệp từ:
     `phone_app/app_ios/ESPRainMaker/RainMaker+NotificationExtension/Extensions/Array<String>+CombinedString.swift`
   - Thành:
     `phone_app/app_ios/ESPRainMaker/RainMaker+NotificationExtension/Extensions/Array_String_+CombinedString.swift`
2. **Cập nhật dự án Xcode**:
   - Thay thế toàn bộ 6 vị trí tham chiếu tên tệp cũ trong `phone_app/app_ios/ESPRainMaker/ESPRainMaker.xcodeproj/project.pbxproj` sang tên tệp mới để đảm bảo dự án iOS vẫn build bình thường.
3. **Bảo vệ cấu hình Git**:
   - Cập nhật `.gitignore` để loại bỏ các tệp sinh tự động của extension C/C++ (`**/.clangd*`).

---

## 2. Tại sao phải làm vậy?

- **Nguyên nhân cốt lõi**:
  - Hệ thống tệp **NTFS của hệ điều hành Windows** cấm tuyệt đối 9 ký tự đặc biệt trong tên tệp: `< > : " / \ | ? *`.
  - Repo gốc của Espressif (`book-esp32c3-iot-projects`) có chứa tệp `Array<String>+CombinedString.swift` được commit từ máy tính macOS.
  - Khi bất kỳ sinh viên/thành viên nào dùng máy tính Windows chạy lệnh:
    ```bash
    git clone https://github.com/phamdinhchien210104/PBL5_stevejobvn.git
    ```
    Git trên Windows sẽ văng lỗi nghiêm trọng:
    ```text
    error: invalid path '.../Array<String>+CombinedString.swift'
    fatal: unable to checkout working tree
    warning: Clone succeeded, but checkout failed.
    ```
    Hậu quả: Thư mục dự án sau khi clone về bị trống rỗng, không thể checkout nhánh `main` và không thể làm việc được.
- **Giải pháp**:
  - Đổi ký tự `<` và `>` thành dấu gạch dưới `_`, đồng bộ file dự án Xcode để tương thích 100% với cả Windows, Linux lẫn macOS mà không làm mất bất kỳ dòng code nào.

---

## 3. Nguồn tham khảo ở đâu?

1. **Microsoft Windows Win32 Namespace Naming Conventions**:
   - Quy chuẩn đặt tên tệp NTFS trên Windows: [Naming Files, Paths, and Namespaces (Microsoft Learn)](https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file#naming-conventions).
2. **Git for Windows Bug Tracker & Core Documentation**:
   - Tài liệu về cấu hình `core.protectNTFS`: [git-config core.protectNTFS documentation](https://git-scm.com/docs/git-config#Documentation/git-config.txt-coreprotectNTFS).
   - Espressif IoT Projects Issue Tracker liên quan đến lỗi clone trên Windows.
