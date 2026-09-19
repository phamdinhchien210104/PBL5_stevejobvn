# 🚀 PBL5: Thiết kế Hệ thống Nhúng & IoT Thông Minh
> **Đề án Nâng cấp & Hiện đại hoá Hệ thống IoT Đa mục tiêu (ESP32-S3 & ESP32-C3)**  
> *Kế thừa và cải tiến từ giáo trình "ESP32-C3 Wireless Adventure: A Comprehensive Guide to IoT" (Espressif Systems)*

---

## 📌 1. Giới thiệu Dự án

Dự án này là bài tập lớn môn học **Hệ thống Nhúng (PBL5)**. Mục tiêu của dự án là vừa nghiên cứu chuyên sâu các khía cạnh cốt lõi của hệ thống nhúng (FreeRTOS, Drivers, Wi-Fi Station/Provisioning, Cloud IoT RainMaker, Tối ưu hóa Flash/RAM, Diagnostics/Insights), vừa nâng cấp, bảo trì và tái cấu trúc toàn bộ codebase cũ (4 năm chưa cập nhật) theo chuẩn kỹ sư phần mềm chuyên nghiệp tại doanh nghiệp.

### 🌟 Điểm Cải Tiến & Hiện Đại Hóa Nổi Bật:
- **Hỗ trợ Đa mục tiêu phần cứng (Multi-Target Hardware)**: Tương thích hoàn hảo cả **ESP32-S3** (Xtensa Dual-core) và **ESP32-C3** (RISC-V Single-core). Các thành viên trong nhóm có board nào đều chạy được board đó mà không làm xung đột code của nhau.
- **Tương thích ESP-IDF v5.x / v6.x**: Cập nhật toàn bộ các breaking changes về CMake, Driver API, Timer, và GPIO.
- **Mô hình Multi-Root Workspace**: Quản lý 7 subproject độc lập qua `pbl5.code-workspace`, chuyển đổi dự án 1-click trên thanh trạng thái VS Code / Antigravity IDE.
- **Tự động hóa CI/CD**: Tích hợp GitHub Actions tự động kiểm thử biên dịch song song cả hai chip S3 và C3 trên mỗi Pull Request.

---

## 📂 2. Cấu trúc Dự án (Monorepo Layout)

```text
PBL5_stevejobvn/
├── .github/workflows/ci.yml       # CI/CD tự động kiểm thử build S3 & C3
├── .vscode/settings.json          # Cấu hình IDE & ESP-IDF Extension
├── docs/decisions/                # Tài liệu quyết định kiến trúc (ADR)
├── pbl5.code-workspace            # File mở Workspace đa dự án cho IDE
├── device_firmware/               # Mã nguồn 7 chương nhúng
│   ├── 1_blink/                   # Chương 1: Làm quen GPIO, Task, Cấu hình S3/C3
│   ├── 2_light_drivers/           # Chương 2: Driver LED PWM (LEDC), Board Abstraction
│   ├── 3_wifi_connection/         # Chương 3: Kết nối Wi-Fi Station & FreeRTOS Event Group
│   ├── 4_network_config/          # Chương 4: Unified Provisioning qua BLE / SoftAP
│   ├── 5_rainmaker/               # Chương 5: Đám mây ESP RainMaker IoT
│   ├── 6_project_optimize/        # Chương 6: Quản lý Partition NVS, OTA & Tiết kiệm năng lượng
│   ├── 7_insights/                # Chương 7: Giám sát Crash dump & Real-time Diagnostics
│   └── components/                # Thư viện dùng chung (button, driver, storage)
├── phone_app/                     # Ứng dụng di động điều khiển thiết bị
└── test_case/                     # Bộ kịch bản kiểm thử tự động
```

---

## 🛠️ 3. Hướng dẫn Đồng đội Bắt đầu Phát triển (Getting Started)

### Bước 1: Yêu cầu Môi trường
1. Đã cài đặt **ESP-IDF** (phiên bản khuyên dùng: v5.2, v5.3 hoặc v6.0).
2. Trình soạn thảo: **Antigravity IDE** hoặc **Visual Studio Code**.
3. Cài Extension **ESP-IDF** từ Extension Marketplace.

### Bước 2: Mở Dự án Đúng Cách (Bắt buộc dùng Workspace)
> [!IMPORTANT]
> **Không mở thư mục dạng thông thường (`Open Folder`)**. Vì dự án gồm nhiều chapter độc lập, bạn hãy mở bằng file Workspace:

1. Vào menu: **`File`** $\rightarrow$ **`Open Workspace from File...`**
2. Chọn file: **`pbl5.code-workspace`** tại thư mục gốc.
3. Lúc này cột Explorer sẽ hiển thị tách bạch từng chương (`🌟 1_blink`, `💡 2_light_drivers`,...), và Extension ESP-IDF sẽ tự động kích hoạt 100%.

### Bước 3: Cấu hình Extension trên Máy Cá nhân (Tránh Xung đột Git)
> [!IMPORTANT]
> **Nguyên tắc cốt lõi của nhóm**: Mỗi bạn có đường dẫn cài đặt khác nhau (ổ `C:` hay `D:`) và cổng COM khác nhau. **Tuyệt đối không commit đường dẫn tuyệt đối hoặc cổng COM cá nhân lên file `.vscode/settings.json` của Git**. Thay vào đó, hãy cấu hình 1 lần duy nhất vào **User Settings** theo các bước sau:

1. **Kích hoạt trình thiết lập tự động**:
   - Nhấn phím `F1` (hoặc `Ctrl + Shift + P`) $\rightarrow$ Gõ và chọn: **`ESP-IDF: Configure ESP-IDF Extension`**.
2. **Chọn bộ cài ESP-IDF có sẵn trên máy bạn**:
   - Chọn **"Find ESP-IDF in your system"** (hoặc "Existing Setup").
   - Extension sẽ tự động quét và hiện ra bộ cài đặt trên máy bạn (ví dụ: `D:\esp\v6.0.2\esp-idf` hoặc `C:\esp\...`).
   - Bấm **Save / Complete**.
   - *Toàn bộ đường dẫn này sẽ tự động lưu vào User Settings riêng của bạn (`%APPDATA%\Code\User\settings.json`), nằm ngoài Git và không bao giờ bị ghi đè hay xung đột khi các thành viên pull/push code!*
3. **Cấu hình Chip Mục tiêu và Cổng COM dưới thanh trạng thái (Status Bar)**:
   - **Device Target**: Chọn `esp32s3` (cho kit ESP32-S3-DevKitC-1-N16R8) hoặc `esp32c3` (cho kit ESP32-C3).
   - **Port**: Chọn cổng COM cắm kit của bạn (`COM3`, `COM4`, `COM8`,...).
   - **Flash Type**: Chọn `UART`.

---

## 🕹️ 4. Thao tác Biên dịch & Nạp Firmware trên Extension

Dưới góc trái thanh trạng thái (Status Bar) dưới đáy màn hình, bạn sẽ thấy các nút chức năng:

1. **Chọn Chapter đang học/làm việc**: Click vào nút `$(folder) <tên_project>` (ví dụ: `🌟 1_blink`) để chọn thư mục làm việc hiện tại.
2. **Chọn Chip Mục tiêu (Device Target)**:
   - Click vào biểu tượng chip trên status bar (hoặc nhấn `Ctrl + Shift + P` $\rightarrow$ gõ `ESP-IDF: Set Espressif Device Target`).
   - Nếu bạn dùng kit **ESP32-S3** $\rightarrow$ Chọn `esp32s3`.
   - Nếu bạn dùng kit **ESP32-C3** $\rightarrow$ Chọn `esp32c3`.
   - *Extension sẽ tự nạp file cấu hình tối ưu tương ứng `sdkconfig.defaults.<target>`*.
3. **Chọn Cổng COM (Port)**: Click vào biểu tượng cổng cắm `$(plug)` $\rightarrow$ Chọn cổng COM của kit ESP32 cắm vào máy bạn.
4. **Biên dịch (Build)**: Click vào biểu tượng chiếc cờ lê / thùng đồ nghề `$(gear) Build` (hoặc bấm phím tắt `Ctrl + E`, sau đó bấm `B`).
5. **Nạp Firmware (Flash)**: Click biểu tượng tia sét `$(zap) Flash`.
6. **Mở Log Giám sát (Monitor)**: Click biểu tượng màn hình terminal `$(terminal) Monitor` (hoặc bấm phím tắt `Ctrl + E`, sau đó bấm `M`). Để thoát Monitor, bấm `Ctrl + ]`.

---

## 🤝 5. Quy chuẩn Làm việc Nhóm với Git (Git Workflow)

Để phối hợp nhóm hiệu quả và không bao giờ làm xung đột hay đè mất code của nhau, nhóm tuân thủ chặt chẽ mô hình **Trunk-Based Development**:

### 5.1. Nguyên tắc cốt lõi:
- Nhánh `main` luôn là nhánh ổn định, **phải luôn build pass**.
- **Không bao giờ push code trực tiếp lên `main`**. Mọi tính năng hay sửa lỗi đều phải làm trên nhánh riêng (Feature Branch) rồi tạo **Pull Request (PR)**.

### 5.2. Các bước làm một tính năng mới:
```bash
# 1. Cập nhật code mới nhất từ nhánh main
git checkout main
git pull origin main

# 2. Tạo nhánh mới cho công việc của bạn
# Đặt tên: feat/<tên-chức-năng> hoặc fix/<tên-lỗi>
git checkout -b feat/add-button-driver-c3

# 3. Code, test cẩn thận trên kit và commit từng bước nhỏ
git add <các-file-đã-sửa>
git commit -m "feat(button): add debouncing logic for esp32c3 gpio9"

# 4. Đẩy nhánh lên GitHub
git push -u origin feat/add-button-driver-c3
```

### 5.3. Chuẩn viết Commit Message (Conventional Commits):
Format: `<loại>: <mô tả ngắn gọn>`
- `feat`: Tính năng mới (ví dụ: `feat: support esp32s3 rgb led driver`).
- `fix`: Sửa lỗi (ví dụ: `fix: resolve riscv compiler missing binary issue`).
- `refactor`: Tái cấu trúc code nhưng không đổi logic (ví dụ: `refactor: extract board pins into header`).
- `docs`: Cập nhật tài liệu, README, ADR (ví dụ: `docs: update getting started guide`).
- `chore`: Cấu hình build, toolchain, gitignore (ví dụ: `chore: add github actions ci`).

### 5.4. Quy tắc Giữ sạch Git Repository (Git Hygiene):
- **Không commit đường dẫn máy cá nhân**: File `.vscode/settings.json` trong Git chỉ lưu các thiết lập chuẩn chung. Nếu máy bạn có tùy chỉnh đường dẫn riêng ở Workspace level, hãy dùng lệnh sau để Git bỏ qua các thay đổi cục bộ:
  ```bash
  git update-index --skip-worktree .vscode/settings.json
  ```
- **Tuyệt đối không commit tệp index/cache nhị phân**: Không commit thư mục `.clangd/` hay các file nhị phân `*.idx` của clangd vào repo.
- **Không commit thư mục `build/` hay `sdkconfig.old`**: Luôn đảm bảo chỉ commit mã nguồn và tài liệu cần thiết.

---

## ⚙️ 6. Quy trình Tự động hóa CI/CD (GitHub Actions)

Dự án đã tích hợp quy trình kiểm thử tự động tại [.github/workflows/ci.yml](file:///.github/workflows/ci.yml):

- **Khi nào CI chạy?** Mỗi khi có một commit được push hoặc một Pull Request được mở hướng vào nhánh `main`.
- **CI kiểm tra điều gì?**
  - Khởi tạo môi trường Docker chuẩn của Espressif (`espressif/idf:latest`).
  - Tự động chạy ma trận kiểm thử: Build firmware đồng thời cho cả **`esp32s3`** và **`esp32c3`**.
- **Quy tắc vàng**: **Chỉ được merge Pull Request khi biểu tượng CI hiện dấu tích xanh ✅**. Nếu có dấu gạch chéo đỏ ❌, người tạo PR phải xem log và sửa lỗi ngay trên nhánh của mình.

---

## 🗺️ 7. Lộ trình Thực hiện & Trạng thái các Chương (Roadmap)

| Chương | Nội dung Kiến thức Hệ thống Nhúng | Trạng thái ESP32-S3 | Trạng thái ESP32-C3 | Ghi chú Nâng cấp |
| :---: | :--- | :---: | :---: | :--- |
| **1_blink** | FreeRTOS Task, GPIO Output, Kconfig đa mục tiêu | ✅ Hoàn thành | ✅ Hoàn thành | Tối ưu `COMPONENTS`, Kconfig GPIO S3/C3 |
| **2_light_drivers** | LEDC (PWM), Abstraction Layer cho Board S3 & C3 | ⏳ Đang tiến hành | ⏳ Đang tiến hành | Đã tạo `board_esp32s3_devkitc.h` |
| **3_wifi_connection** | Wi-Fi Station Mode, FreeRTOS Event Group, Reconnect | 🔜 Sắp tới | 🔜 Sắp tới | Nâng cấp API Wi-Fi IDF v5/v6 |
| **4_network_config** | Unified Provisioning (Cấu hình Wi-Fi qua BLE) | 🔜 Sắp tới | 🔜 Sắp tới | Bluetooth NimBLE stack update |
| **5_rainmaker** | ESP RainMaker Cloud (Nodes, Devices, Params) | 🔜 Sắp tới | 🔜 Sắp tới | Component Manager RainMaker |
| **6_project_optimize** | Tối ưu Flash/RAM, Phân vùng NVS & Sleep Mode | 🔜 Sắp tới | 🔜 Sắp tới | Tối ưu hóa hiệu năng |
| **7_insights** | Crash dump, Diagnostics & Giám sát từ xa | 🔜 Sắp tới | 🔜 Sắp tới | ESP Insights v1.x+ |

---
*Chúc cả nhóm phối hợp nhịp nhàng, làm chủ hệ thống nhúng và đạt kết quả cao nhất trong đồ án PBL5!* 🎯
