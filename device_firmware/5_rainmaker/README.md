# Chapter 9: Remote Control Through ESP RainMaker (`device_firmware/5_rainmaker`)

> **Căn cứ tài liệu**: Mục *9.4 Practice: Remote Control through ESP RainMaker* — Sách *ESP32-C3 Wireless Adventure: A Comprehensive Guide to IoT* (Espressif Systems).  
> **Nền tảng**: ESP-IDF v6.0.2 | GCC 15 | FreeRTOS | ESP RainMaker v1.16.1 (AWS IoT Core Mutual TLS Port 8883).  
> **Mục tiêu phần cứng (Dual-Target)**:
> - **ESP32-C3-DevKitM-1** (RISC-V): Nút Boot **GPIO 9**, LED WS2812B **GPIO 4**.
> - **ESP32-S3-DevKitC-1-N16R8** (Xtensa): Nút Boot **GPIO 0**, LED WS2812B **GPIO 4**.
> - **Dải LED**: Thanh 8 hạt NeoPixel WS2812B (`CJMCU-2812-8`) điều khiển qua **Hardware SPI2 DMA @ 3.2MHz**.
> **Tài liệu kiến trúc (ADR)**: [ADR-006: Điều Khiển Từ Xa ESP RainMaker Đám Mây](../../docs/decisions/ADR-006-esp-rainmaker-cloud-control.md) | [Sơ đồ tương tác Archify](../../docs/decisions/diagrams/ADR-006-esp-rainmaker-cloud-control.html).

---

## 1. So Sánh Kiến Trúc: Mã Nguồn Cũ vs Mã Nguồn Cải Tiến Chuẩn PBL5

| Thành phần / Tiêu chí | Mã nguồn gốc trong sách (Cũ) | Mã nguồn cải tiến chuẩn PBL5 (Mới) |
| :--- | :--- | :--- |
| **Phiên bản ESP RainMaker** | Thư viện RainMaker cũ nhúng thủ công, không tương thích ESP-IDF v6. | **ESP RainMaker v1.16.1** qua IDF Component Manager manifest, tối ưu mbedTLS dynamic buffer. |
| **Mô hình TSL Lightbulb** | Tham số cơ bản (chỉ Bật/Tắt). | **Đầy đủ 4 tham số TSL chuẩn**: `Power` (bool), `Brightness` (0–100%), `Hue` (0–360°), `Saturation` (0–100%). |
| **Cơ chế Cấp chứng chỉ (Claiming)** | Nạp thủ công file nhị phân chứng chỉ X.509 qua cổng Serial bằng máy tính. | **Assisted Claiming tự động qua BLE**: Tải chứng chỉ bảo mật trực tiếp từ Cloud lưu vào phân vùng `fctry` (24KB). |
| **Đồng bộ Device Shadow (2-Way)** | Chỉ nhận lệnh 1 chiều từ Cloud (Downlink), không đồng bộ khi bấm nút thật. | **Đồng bộ hóa 2 chiều tức thì**: Bấm nút Boot trên mạch vật lý tự động cập nhật lên Cloud Shadow qua `esp_rmaker_param_update_and_report`. |
| **Dịch vụ Hệ thống nâng cao** | Không có hoặc cấu hình rời rạc. | **Bật trọn bộ 5 dịch vụ chuẩn**: SNTP Time Sync (`pool.ntp.org`), Timezone, Offline Scheduling (hẹn giờ không cần mạng), OTA Rollback Safe, và LAN Local Control. |
| **Điều khiển LED WS2812B** | RMT driver phiên bản cũ dễ lỗi trên chip mới hoặc PWM LED đơn sắc. | **Hardware SPI2 DMA @ 3.2MHz**: Khử giật khung hình, hiển thị màu sắc rực rỡ và chuẩn xác theo không gian màu HSV. |
| **Nút bấm vật lý đa cử chỉ** | Polling nút đơn giản, thiếu chống dội (debounce). | **IoT Button Driver HAL**: Click (Bật/Tắt), Double Click (Xoay 6 màu HSV), Long Press >3s (Khôi phục cài đặt gốc Factory Reset). |
| **Phân vùng Flash (Partitioning)** | 1.8MB OTA dễ cảnh báo tràn bộ nhớ khi thêm tính năng. | **Phân vùng 4MB tối ưu**: `ota_0` & `ota_1` đạt **1920KB** (căn chỉnh 64K chuẩn Flash MMU), dư 9%–28% không gian an toàn. |

---

## 2. Các Điểm Chỉnh Sửa & Hiện Thực Cốt Lõi Trên ESP-IDF v6.0.2

1. **Bộ Đệm Zero-Touch Adapters (`components/`)**:
   - `components/wifi_provisioning/`: Forwarding adapter chuyển tiếp trong suốt các API cũ sang component `network_provisioning` hiện đại của ESP-IDF v6.0.2.
   - `components/qrcode/`: Tích hợp trực tiếp engine tạo mã QR của `espressif__qrcode`, giải quyết xung đột CMake namespace mà **không chạm vào** thư mục chia sẻ `device_firmware/components/`.
2. **Khởi tạo Node và Light Device ([`app_main.c`](main/app_main.c))**:
   - `esp_rmaker_node_init()`: Đặt tên node `"ESP RainMaker Smart Light"` (Type: `"Lightbulb"`).
   - Đăng ký Callback Downlink `write_cb()`: Lắng nghe lệnh từ Cloud/App, phân tích và gọi `app_light_set_power()`, `app_light_set_brightness()`, `app_light_set_hue()`, `app_light_set_saturation()`.
3. **Kích hoạt Dịch vụ Đồng bộ & Ngoại tuyến**:
   - `esp_rmaker_time_sync_init()` & `esp_rmaker_timezone_service_enable()`: Lấy giờ chuẩn quốc tế để phục vụ hẹn giờ.
   - `esp_rmaker_schedule_enable()`: Hẹn giờ bật/tắt cục bộ ngay trên chip (kể cả khi mất mạng Wi-Fi/Internet đèn vẫn chạy đúng lịch).
   - `esp_rmaker_ota_enable()`: Cập nhật OTA an toàn qua chứng chỉ `ota_server_cert`.
   - `esp_rmaker_system_service_enable()`: Cho phép khởi động lại và reset từ xa.
4. **Đồng bộ Phần Cứng & Device Shadow ([`app_driver.c`](main/app_driver.c))**:
   - Nút Boot HAL (S3 GPIO 0 / C3 GPIO 9):
     - Single Click: Bật/Tắt $\rightarrow$ gọi `esp_rmaker_param_update_and_report(power_param, ...)`.
     - Double Click: Xoay vòng các bước màu (`0°`, `60°`, `120°`, `180°`, `240°`, `300°`) $\rightarrow$ báo cáo `Hue` & `Saturation`.
     - Long Press >3s: Kích hoạt `esp_rmaker_factory_reset()` (xóa mạng và chứng chỉ, đưa thiết bị về trạng thái ban đầu).

---

## 3. Hướng Dẫn Biên Dịch & Nạp Firmware

### Bước 1: Chuẩn bị Môi trường Build ESP-IDF v6.0.2

Trong PowerShell, sử dụng lệnh một dòng chuẩn:

```powershell
$env:IDF_TOOLS_PATH = "D:\Espressif"; . "D:\esp\v6.0.2\esp-idf\export.ps1"; idf.py -C d:\Document\PBL5_stevejobvn\device_firmware\5_rainmaker build
```

*(Hoặc sử dụng Fast Build không kiểm tra môi trường)*:
```powershell
$paths = @("D:\Espressif\tools\ninja\1.12.1", "D:\Espressif\tools\cmake\4.0.3\bin", "D:\Espressif\tools\python\v6.0.2\venv\Scripts", "D:\Espressif\tools\riscv32-esp-elf\esp-15.2.0_20251204\riscv32-esp-elf\bin", "D:\Espressif\tools\xtensa-esp-elf\esp-15.2.0_20251204\xtensa-esp-elf\bin", "D:\esp\v6.0.2\esp-idf\tools", "D:\Espressif\tools\idf-exe\1.0.3"); $env:PATH = ($paths -join ";") + ";" + $env:PATH; $env:IDF_PATH = "D:\esp\v6.0.2\esp-idf"; $env:IDF_TOOLS_PATH = "D:\Espressif"; idf.py -C d:\Document\PBL5_stevejobvn\device_firmware\5_rainmaker build
```

*(Đối với ESP32-S3, trước khi build chạy lệnh: `idf.py -C device_firmware/5_rainmaker set-target esp32s3`)*.

### Bước 2: Nạp Firmware & Mở Serial Monitor

Kết nối mạch ESP32 vào cổng USB (mặc định ví dụ `COM3`):

```powershell
idf.py -C d:\Document\PBL5_stevejobvn\device_firmware\5_rainmaker -p COM3 flash monitor
```

---

## 4. Hướng Dẫn Kiểm Thử Trên Phần Cứng Thực Tế

### Bước 1: Quét Mã QR & Cấp Phát Mạng (Assisted Claiming)
1. Cài đặt ứng dụng **ESP RainMaker** từ **App Store** (iOS) hoặc **Google Play Store** (Android) và đăng nhập tài khoản.
2. Bật **Bluetooth** và **Vị trí (Location)** trên smartphone.
3. Khi mạch khởi động lần đầu, màn hình Terminal Serial Monitor sẽ hiển thị một **Mã QR (ASCII QR Code)** và URL:
   ```text
   I (2450) app_wifi: ========================================================
   I (2460) app_wifi: Scan QR code on RainMaker App to configure Wi-Fi:
   ... [MÃ QR ASCII HIỂN THỊ TRÊN CONSOLE] ...
   I (2520) app_wifi: Or open link: https://rainmaker.espressif.com/qrcode.html?...
   I (2530) app_wifi: ========================================================
   ```
4. Trên ứng dụng di động, bấm **Add Device** (dấu `+`) $\rightarrow$ Quét mã QR trên màn hình console $\rightarrow$ Chọn mạng Wi-Fi 2.4GHz và nhập mật khẩu.
5. Quá trình **Assisted Claiming** tự động tải chứng chỉ TLS X.509 về phân vùng `fctry`. Khi kết nối thành công, terminal sẽ in:
   ```text
   I (12340) rainmaker: RainMaker Smart Light initialized successfully! Online and connected to Cloud.
   ```

---

### Bước 2: Kiểm Thử Các Tính Năng Điều Khiển

| STT | Kịch Bản Kiểm Thử | Thao Tác Thực Hiện | Hiện Tượng Quan Sát Mong Đợi |
| :---: | :--- | :--- | :--- |
| **1** | **Điều khiển Bật/Tắt từ Cloud** | Nhấn công tắc Power trên App điện thoại. | Đèn WS2812B trên mạch bật/tắt tức thì. Terminal in: `==> [Cloud Downlink] Light - Power: ON/OFF`. |
| **2** | **Chỉnh Độ Sáng (Dimming)** | Kéo thanh trượt `Brightness` (0–100%). | Độ sáng thanh LED tăng/giảm mượt mà theo giá trị chọn. |
| **3** | **Đổi Màu Sắc (Color Wheel)** | Xoay vòng chọn màu HSV trên App. | Thanh LED đổi màu chuẩn xác theo mã màu (Đỏ, Xanh lá, Xanh dương, Tím, Vàng,...). |
| **4** | **Phản Hồi Hai Chiều (Click Nút Boot)** | Nhấn **1 lần (Single click)** nút Boot trên board mạch. | Đèn lật trạng thái Bật/Tắt. **Giao diện App trên điện thoại tự động chuyển trạng thái theo** mà không cần kéo vuốt làm mới. |
| **5** | **Xoay Vòng Màu (Double Click)** | Nhấn **nhấp đúp (Double click)** nút Boot trên board mạch. | Đèn chuyển sang màu kế tiếp trong 6 màu cơ bản. Thông số màu trên App tự động cập nhật tương ứng. |
| **6** | **Hẹn Giờ Tự Động (Schedule)** | Tạo lịch trong mục *Schedules* trên App (ví dụ: Bật đèn sau 1 phút). | Đúng giờ hẹn đèn tự động chuyển trạng thái (kể cả khi ngắt đường truyền Internet ra ngoài). |
| **7** | **Khôi Phục Cài Đặt Gốc (Phím Cứng)** | Nhấn **giữ nút Boot > 3 giây**. | Đèn nhấp nháy đỏ báo hiệu, thiết bị xóa cấu hình Wi-Fi trong NVS, reset và quay lại chế độ phát mã QR / BLE chờ cấu hình mới. |
| **8** | **Đổi Wi-Fi Từ Xa Trên App (System Service)** | Mở thiết bị trong App $\rightarrow$ Cài đặt (Menu góc phải) $\rightarrow$ **Reset Wi-Fi** (hoặc Remove Device). | Cloud gửi lệnh xuống, ESP32-C3 tự động xóa NVS và khởi động lại sau 2 giây, phát lại BLE để nạp Wi-Fi mới mà không cần chạm vào nút bấm vật lý. |
| **9** | **Chọn Hiệu Ứng (Lighting Effect)** | Chọn mục **Effect** trên App (`Static`, `Blink`, `Breathing`, `Rainbow`). | Đèn LED chuyển hiệu ứng tức thời: nhấp nháy, thở mờ dần, xoay dải màu cầu vồng hoặc sáng tĩnh. |


