# ADR-001: Nâng cấp Toàn diện Hệ thống Build, Hỗ trợ Đa mục tiêu ESP32-S3/C3 và Thiết lập Quy chuẩn Phát triển

- **Trạng thái**: Đã phê duyệt (Accepted)
- **Ngày quyết định**: 2026-09-16
- **Phạm vi**: Toàn bộ dự án (Root, Toolchain, CI/CD, Module `1_blink` & `2_light_drivers`)
- **Sơ đồ kiến trúc tương tác (Archify)**: [Xem sơ đồ trực quan (HTML)](./diagrams/ADR-001-modernization-and-multi-target.html)

---

## 1. Quyết định: Cấu trúc Multi-Root Workspace (`pbl5.code-workspace`) & Kích hoạt Extension

### 1. Sửa, ghi thêm, cập nhật những gì?
- Tạo file [pbl5.code-workspace](file:///d:/Document/PBL5_stevejobvn/pbl5.code-workspace) ở thư mục gốc, phân chia 7 chapter (`1_blink`, `2_light_drivers`,...) thành từng folder độc lập.
- Thêm cấu hình `"idf.extensionActivationMode": "always"` vào [.vscode/settings.json](file:///d:/Document/PBL5_stevejobvn/.vscode/settings.json) và User settings của Antigravity IDE.

### 2. Tại sao phải làm vậy?
- Repo gốc là dạng Monorepo chứa nhiều firmware con bên trong `device_firmware/`. Extension ESP-IDF ở chế độ mặc định (`detect`) tìm kiếm `CMakeLists.txt` tại thư mục gốc, nếu không thấy sẽ hoãn kích hoạt và gây lỗi *"There is no data provider registered that can provide view data"*.
- Mở qua Multi-Root Workspace giúp Extension nhận diện chính xác từng dự án con, cho phép chuyển đổi chapter và chip mục tiêu chỉ với 1-click trên thanh Status Bar.

### 3. Nguồn thông tin:
- Mã nguồn kích hoạt của Extension ESP-IDF: `C:\Users\tanmi\.antigravity-ide\extensions\espressif.esp-idf-extension-2.2.0-universal\dist\extension.js` (hàm kiểm tra `idf.extensionActivationMode` và `workspaceFolders`).
- Tài liệu chính thức Espressif VS Code Extension: [Working with Multiple Projects](https://github.com/espressif/vscode-esp-idf-extension/blob/master/docs/MULTI_PROJECTS.md).

---

## 2. Quyết định: Hỗ trợ Đa mục tiêu Phần cứng (ESP32-S3 và ESP32-C3)

### 1. Sửa, ghi thêm, cập nhật những gì?
- **Kconfig**: Mở rộng dải chân `BLINK_GPIO` trong [1_blink/main/Kconfig.projbuild](file:///d:/Document/PBL5_stevejobvn/device_firmware/1_blink/main/Kconfig.projbuild) lên `0 48` cho S3 và `0 21` cho C3.
- **Tách cấu hình mục tiêu**: Tạo [sdkconfig.defaults.esp32s3](file:///d:/Document/PBL5_stevejobvn/device_firmware/1_blink/sdkconfig.defaults.esp32s3) và [sdkconfig.defaults.esp32c3](file:///d:/Document/PBL5_stevejobvn/device_firmware/1_blink/sdkconfig.defaults.esp32c3).
- **Tầng trừu tượng Board**: Tạo [board_esp32s3_devkitc.h](file:///d:/Document/PBL5_stevejobvn/device_firmware/2_light_drivers/main/include/board_esp32s3_devkitc.h) (Boot button GPIO 0, PWM GPIO 4, 5, 6, 7, 15) và cập nhật [CMakeLists.txt](file:///d:/Document/PBL5_stevejobvn/device_firmware/2_light_drivers/main/CMakeLists.txt) để tự động chọn header theo target.

### 2. Tại sao phải làm vậy?
- Dự án sách gốc viết cứng chỉ cho ESP32-C3. Trong nhóm làm việc thực tế, người dùng sử dụng chip ESP32-S3 (Xtensa Dual-core) trong khi các thành viên khác dùng ESP32-C3 (RISC-V).
- Các chip khác nhau hoàn toàn về dải chân GPIO và chân nút nhấn Boot (S3 dùng GPIO 0, C3 dùng GPIO 9). Việc tách tầng trừu tượng giúp dùng chung 100% logic điều khiển mà không xung đột chân.

### 3. Nguồn thông tin:
- ESP32-S3 Datasheet & Schematics ESP32-S3-DevKitC-1.
- ESP-IDF Build System Documentation: [Target-dependent sdkconfig defaults](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/build-system.html#custom-sdkconfig-defaults).

---

## 3. Quyết định: Khắc phục lỗi Crash Trình biên dịch GCC 15.2.0 bằng `set(COMPONENTS main)`

### 1. Sửa, ghi thêm, cập nhật những gì?
- Trong [1_blink/CMakeLists.txt](file:///d:/Document/PBL5_stevejobvn/device_firmware/1_blink/CMakeLists.txt), nâng cấp `cmake_minimum_required(VERSION 3.16)` và thêm dòng `set(COMPONENTS main)` trước khi include `project.cmake`.

### 2. Tại sao phải làm vậy?
- Bộ toolchain mới nhất của ESP-IDF v6.0.2 sử dụng GCC 15.2.0. Khi biên dịch mặc định toàn bộ component hệ thống, GCC 15 gặp lỗi Segfault nội bộ (*internal compiler error during RTL pass: ira*) tại file `esp_lcd_panel_rgb.c`.
- Khai báo `set(COMPONENTS main)` yêu cầu CMake chỉ biên dịch các component mà `main` thực sự phụ thuộc (FreeRTOS, GPIO, Log), loại bỏ hoàn toàn `esp_lcd`, khắc phục triệt để lỗi crash và giảm số lượng file cần biên dịch từ 1062 xuống còn 543 file (tăng tốc build hơn 50%).

### 3. Nguồn thông tin:
- Log lỗi biên dịch thực tế của Ninja & GCC 15: `internal compiler error: Segmentation fault in rgb_panel_draw_bitmap`.
- Tài liệu ESP-IDF CMake Component Requirements: [Minimizing Component Dependencies](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/build-system.html#renaming-main-component).

---

## 4. Quyết định: Sửa chữa Bộ công cụ GNU Binutils cho RISC-V trên Windows

### 1. Sửa, ghi thêm, cập nhật những gì?
- Bổ sung các bản sao file thực thi chuẩn GNU: `riscv32-esp-elf-ld.exe`, `objcopy.exe`, `ar.exe`.
- Thay thế các wrapper bị lỗi panic (`as.exe`, `objdump.exe`) bằng nhị phân GNU chính thức tương ứng (`as-xespv2p1.exe`, `objdump-xespv2p1.exe`) trong thư mục `D:\Espressif\tools\riscv32-esp-elf`.

### 2. Tại sao phải làm vậy?
- Gói cài đặt RISC-V trên Windows bị thiếu các file nhị phân của bộ liên kết (Linker `ld`) và hai wrapper của Espressif viết bằng Rust bị crash với lỗi `assertion failed: Failed to get path name. Error code: 2` mỗi khi chạy `ldgen.py` hoặc `objdump`.
- Sửa trực tiếp giúp môi trường có thể biên dịch ra mã máy RISC-V cho ESP32-C3 một cách ổn định lâu dài.

### 3. Nguồn thông tin:
- Python traceback của script `D:\esp\v6.0.2\esp-idf\tools\ldgen\ldgen.py` và log kiểm tra compiler `CMakeTestCCompiler.cmake`.
- Cấu trúc thư mục chuẩn của bộ `xtensa-esp-elf` đối chiếu với `riscv32-esp-elf`.

---

## 5. Quyết định: Thiết lập Đường ống Tự động hóa CI/CD với GitHub Actions

### 1. Sửa, ghi thêm, cập nhật những gì?
- Tạo file workflow [.github/workflows/ci.yml](file:///d:/Document/PBL5_stevejobvn/.github/workflows/ci.yml).
- Cấu hình ma trận kiểm thử (Matrix Strategy) chạy đồng thời trên cả hai kiến trúc `esp32s3` và `esp32c3` sử dụng Docker image chính thức `espressif/idf:latest`.

### 2. Tại sao phải làm vậy?
- Dự án trước đây sử dụng `.gitlab-ci.yml` trỏ vào server nội bộ của Espressif và bản IDF v4.3.2 đã lỗi thời.
- Dự án nhóm trên GitHub cần quy trình kiểm thử tự động (Shift-Left). Mỗi khi có thành viên đẩy code hoặc mở Pull Request, hệ thống sẽ tự động kiểm tra xem code mới có làm gãy build của chip kia hay không.

### 3. Nguồn thông tin:
- Kỹ năng tiêu chuẩn doanh nghiệp `/ci-cd-and-automation`.
- Espressif Docker Hub Repository (`espressif/idf`).

---

## 6. Quyết định: Khởi tạo Git Tracking và Cập nhật Bộ lọc `.gitignore`

### 1. Sửa, ghi thêm, cập nhật những gì?
- Cập nhật [.gitignore](file:///d:/Document/PBL5_stevejobvn/.gitignore): Bổ sung các thư mục tự sinh của ESP-IDF hiện đại (`managed_components/`, `dependencies.lock`, `sdkconfig.ci`), các file local IDE (`.vscode/*.log`), cùng các thư mục cá nhân theo yêu cầu (`.agents/`, `AGENTS.md`, `.codegraph/`).
- Khởi tạo Git repository trên nhánh `main` (`git init -b main`).
- Thiết lập quy chuẩn commit Conventional Commits (`feat:`, `fix:`, `refactor:`, `docs:`, `chore:`).

### 2. Tại sao phải làm vậy?
- Thư mục ban đầu chưa được khởi tạo Git. Cần đưa vào quản lý phiên bản để sẵn sàng kết nối remote GitHub phục vụ làm việc nhóm.
- Ngăn ngừa việc vô tình commit các thư viện tải về hoặc các file cấu hình nhạy cảm/cá nhân lên repo chung.

### 3. Nguồn thông tin:
- Kỹ năng tiêu chuẩn `/git-workflow-and-versioning`.
- GitHub `.gitignore` template cho ESP-IDF & C/C++.
