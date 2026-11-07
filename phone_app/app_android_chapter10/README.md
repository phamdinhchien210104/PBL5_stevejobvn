# 📱 StevejobVN - PBL5 Android App

Chào mừng bạn đến với mã nguồn của dự án **StevejobVN**, một phần của đồ án PBL5. Đây là ứng dụng Android được cấu hình cho các tính năng liên quan đến thiết bị ngoại vi và kết nối mạng.

## 🌟 Tổng quan
Dựa vào cấu hình hiện tại, ứng dụng đã được thiết lập sẵn các quyền cấp hệ thống bao gồm:
- Tương tác qua **Bluetooth** (yêu cầu quyền Bluetooth & Bluetooth Admin).
- Lấy thông tin **Vị trí** (yêu cầu quyền Fine/Coarse Location, bắt buộc đối với tác vụ quét Bluetooth trên các phiên bản Android mới).
- Kết nối mạng/Internet.

## 🛠 Yêu cầu hệ thống (Prerequisites)
- **Android Studio**: Phiên bản mới nhất (Iguana, Jellyfish, Koala...).
- **Java Development Kit (JDK)**: Khuyên dùng JDK 17 (đã tích hợp sẵn trong các bản Android Studio mới).
- **Thiết bị thử nghiệm**: Máy ảo (Emulator) hoặc điện thoại thật chạy Android 8.0 (API 26) trở lên. 
  *(Lưu ý: Để test các chức năng liên quan đến Bluetooth, bạn bắt buộc phải dùng thiết bị điện thoại Android thật).*

## 🚀 Hướng dẫn cài đặt (Installation & Setup)

1. **Clone mã nguồn về máy tính**:
   Mở terminal hoặc Git Bash và chạy lệnh sau:
   ```bash
   git clone -b android_(chapter10) https://github.com/phamdinhchien210104/PBL5_stevejobvn.git
   ```
2. **Mở dự án trong Android Studio**:
   - Khởi động Android Studio.
   - Chọn **Open** và trỏ đến thư mục `PBL5_stevejobvn` (hoặc `stevejobvn`) vừa tải về.
3. **Đồng bộ hóa Gradle (Sync Project)**:
   - Android Studio sẽ tự động tải xuống các thư viện cần thiết. Đợi thanh tiến trình ở góc dưới bên phải hoàn tất.

## 📁 Cấu trúc Project và Chức năng các file chính

Dự án được cấu trúc theo tiêu chuẩn của một ứng dụng Android hiện đại sử dụng ngôn ngữ **Kotlin** và **Gradle Kotlin DSL (.kts)**:

- **`app/src/main/AndroidManifest.xml`**:
  - **Chức năng**: Đây là file "khai sinh" của ứng dụng. Khai báo các quyền quan trọng hệ thống (`ACCESS_FINE_LOCATION`, `ACCESS_COARSE_LOCATION`, `BLUETOOTH`, `INTERNET`, v.v.). Đây cũng là nơi định nghĩa `MainActivity` là màn hình khởi chạy đầu tiên, cấu hình giao diện (Theme) và icon của ứng dụng.

- **`app/src/main/java/com/example/stevejobvn/MainActivity.kt`**:
  - **Chức năng**: File mã nguồn chính, đóng vai trò là Controller/View. Đây là điểm vào (entry point) chứa giao diện đầu tiên khi người dùng mở ứng dụng lên. Mọi logic tương tác ở trang chủ sẽ được gọi từ đây.

- **`app/src/main/java/com/example/stevejobvn/ui/theme/` (Color.kt, Theme.kt, Type.kt)**:
  - **Chức năng**: Chứa cấu hình giao diện sử dụng Jetpack Compose.
    - `Color.kt`: Định nghĩa các mã màu.
    - `Type.kt`: Định nghĩa font chữ và kích cỡ.
    - `Theme.kt`: Quy định giao diện Sáng/Tối (Light/Dark mode) thống nhất cho toàn bộ app.

- **`build.gradle.kts` (Project-level)**:
  - **Chức năng**: File Gradle cấp dự án. Quản lý các plugin và phiên bản công cụ áp dụng chung cho toàn bộ dự án.

- **`app/build.gradle.kts` (Module-level)**:
  - **Chức năng**: File Gradle cấp Module (quan trọng). Chứa các cấu hình biên dịch như (compileSdk, minSdk, targetSdk) và danh sách **tất cả các thư viện (dependencies)** mà ứng dụng đang dùng. Khi cần thêm thư viện mới (ví dụ: Retrofit, Room, Glide), bạn sẽ thêm vào file này.

- **`settings.gradle.kts`**:
  - **Chức năng**: Định nghĩa tên dự án (`StevejobVN`) và khai báo các kho lưu trữ mã nguồn thư viện (Repositories) như `google()`, `mavenCentral()`, và `maven("https://jitpack.io")` (để tải các thư viện mã nguồn mở).

## ⚙️ Hướng dẫn cấu hình và chạy ứng dụng

1. **Chuẩn bị thiết bị**:
   - **Máy ảo (Emulator)**: Đảm bảo phân vùng ổ cứng lưu trữ máy ảo (thường là ổ C) còn trống ít nhất 15-20GB để không bị lỗi "Not enough space to create userdata partition".
   - **Điện thoại thật**: Bật **Chế độ nhà phát triển (Developer Options)** và **Gỡ lỗi qua USB (USB Debugging)**, sau đó cắm cáp kết nối vào máy tính.
2. **Chạy ứng dụng**:
   - Chọn thiết bị của bạn trên thanh công cụ trên cùng của Android Studio.
   - Bấm nút **Run (Tam giác màu xanh lá)** hoặc nhấn phím tắt `Shift + F10`.
3. **Cấp quyền (Permissions)**:
   - Khi chạy ứng dụng thực tế có quét Bluetooth/Vị trí, hệ thống sẽ hiện popup xin quyền. Vui lòng chọn "Cho phép (Allow)" để các tính năng hoạt động chính xác.

## 👤 Tác giả
- **Phạm Đình Chiến** - GitHub: [phamdinhchien210104](https://github.com/phamdinhchien210104)
