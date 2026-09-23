# Thư Mục Sơ Đồ Kiến Trúc Quyết Định (Architecture Decision Diagrams)

> **Dự án**: PBL5 - Hệ Thống Đèn Thông Minh Đa Mục Tiêu (ESP32-S3 & ESP32-C3)  
> **Tiêu chuẩn dựng hình**: Bộ công cụ **Archify** (Profile Showcase, chuẩn đồ họa Vector SVG độc lập, hỗ trợ giao diện Sáng/Tối, hoạt ảnh Trace Motion, Pan/Zoom, chuyển đổi Góc nhìn chuyên đề).  
> **Thời gian cập nhật**: 21/09/2026.

---

## 📌 Giới Thiệu & Mục Đích

Thư mục `docs/decisions/diagrams` lưu trữ toàn bộ các sơ đồ kiến trúc trực quan tương ứng với từng **Bản Ghi Quyết Định Kiến Trúc (Architecture Decision Record - ADR)** trong thư mục cha [`docs/decisions/`](../).

Mỗi quyết định kiến trúc được mô hình hóa qua hai tệp song hành:
1. **Tệp đặc tả cấu trúc dữ liệu (`*.architecture.json`)**: Định nghĩa các thành phần (components), ranh giới mô-đun (boundaries), mối liên kết dữ liệu/tín hiệu (connections) và các chế độ xem chuyên đề (views).
2. **Tệp giao diện trực quan độc lập (`*.html`)**: Trang web độc lập hoàn chỉnh chứa toàn bộ mã SVG, CSS, JavaScript và bảng điều khiển tương tác. Bạn có thể mở trực tiếp bằng bất kỳ trình duyệt web nào (Chrome, Edge, Firefox, Safari) mà không cần cài đặt web server.

---

## 🗺️ Bảng Tra Cứu Toàn Bộ Sơ Đồ Kiến Trúc

| Mã ADR | Tên Quyết Định Kiến Trúc | Phạm Vi & Bản Ghi ADR | Sơ Đồ Trực Quan Tương Tác | Các Góc Nhìn Trọng Tâm (Curated Views) |
| :---: | :--- | :---: | :---: | :--- |
| **ADR-001** | **Hiện đại hóa Monorepo, Đa Mục Tiêu S3/C3 & Tối ưu CMake GCC 15** | [ADR-001.md](../ADR-001-modernization-and-multi-target.md) | [**Xem Sơ Đồ HTML**](./ADR-001-modernization-and-multi-target.html)<br>([JSON](./ADR-001-modernization-and-multi-target.architecture.json)) | 1. *Multi-Target Build Flow* (S3 Xtensa & C3 RISC-V)<br>2. *Compiler Fix & Pruning* (`set(COMPONENTS main)` chống crash GCC 15)<br>3. *DevOps & CI/CD Pipeline* (GitHub Actions Matrix) |
| **ADR-002** | **Khắc Phục Giới Hạn Đường Dẫn Dài NTFS & Git Sparse-Checkout** | [ADR-002.md](../ADR-002-windows-ntfs-clone-fix.md) | [**Xem Sơ Đồ HTML**](./ADR-002-windows-ntfs-clone-fix.html)<br>([JSON](./ADR-002-windows-ntfs-clone-fix.architecture.json)) | 1. *NTFS Path Limitation* (Xử lý lỗi MAX_PATH 260 ký tự)<br>2. *Sparse-Checkout Isolation* (Cách ly submodule dư thừa)<br>3. *Team Git Sync* (Bảo đảm tính toàn vẹn khi cộng tác nhóm) |
| **ADR-003** | **Driver WS2812B Hardware SPI DMA & Nút Bấm Vật Lý Đa Thao Tác** | [ADR-003.md](../ADR-003-light-drivers-and-button-modernization.md) | [**Xem Sơ Đồ HTML**](./ADR-003-light-drivers-and-button-modernization.html)<br>([JSON](./ADR-003-light-drivers-and-button-modernization.architecture.json)) | 1. *Button & Gesture Dispatch Flow* (Single/Double/Long Click)<br>2. *WS2812B Hardware SPI DMA* (Phát xung 800kHz chuẩn xác nano-giây)<br>3. *NVS Flash & Team Git Hygiene* (Lưu trạng thái & chống xung đột) |
| **ADR-004** | **Kết Nối Wi-Fi STA, Cấp Phát Thông Minh BLE, LED Thị Giác & App Boundary** | [ADR-004.md](../ADR-004-wifi-connection-and-visual-feedback.md) | [**Xem Sơ Đồ HTML**](./ADR-004-wifi-connection-and-visual-feedback.html)<br>([JSON](./ADR-004-wifi-connection-and-visual-feedback.architecture.json)) | 1. *Wi-Fi Station & Kconfig (Ch 3)* (Bắt tay WPA2 & FreeRTOS Event Groups)<br>2. *BLE Smart Provisioning (Ch 4)* (Mã QR, PoP `abcd1234`, NVS fast boot, thu hồi RAM BLE)<br>3. *Visual LED Feedback* (4 trạng thái đèn WS2812B: Cyan/Vàng/Xanh/Đỏ)<br>4. *Mobile App Decision* (Phân định ESP BLE Prov vs RainMaker) |
| **ADR-005** | **Điều Khiển Cục Bộ Kênh Đôi Wi-Fi HTTPS mDNS & Bluetooth LE GATT** | [ADR-005.md](../ADR-005-local-control-https-mdns-ble.md) | [**Xem Sơ Đồ HTML**](./ADR-005-local-control-https-mdns-ble.html)<br>([JSON](./ADR-005-local-control-https-mdns-ble.architecture.json)) | 1. *Wi-Fi HTTPS & mDNS (8.5.1)* (mDNS `my_esp_ctrl_device.local`, HTTPS 443 TLS, JSON `status`)<br>2. *Fallback Bluetooth LE GATT (8.5.3)* (Service `0x00FF`, Char `0x0001` Bật/Tắt không cần router)<br>3. *Python Script Verification (8.5.2)* (Kịch bản tự động kiểm thử GET/POST LAN)<br>4. *Hardware HAL & LED Execution* (WS2812B Hardware SPI2 DMA @ 3.2MHz, Nút Boot HAL) |
| **ADR-006** | **Điều Khiển Từ Xa ESP RainMaker Đám Mây, Assisted Claiming & Thiết Bị Bóng (Device Shadow)** | [ADR-006.md](../ADR-006-esp-rainmaker-cloud-control.md) | [**Xem Sơ Đồ HTML**](./ADR-006-esp-rainmaker-cloud-control.html)<br>([JSON](./ADR-006-esp-rainmaker-cloud-control.architecture.json)) | 1. *Cloud MQTT & Device Shadow Downlink (9.4.1)* (AWS IoT Core MQTT, đồng bộ bóng thiết bị TSL HSV & LED thật)<br>2. *BLE Assisted Claiming & Provisioning (9.4.2)* (Cấp phát Wi-Fi & nạp chứng chỉ TLS X.509 vào phân vùng `fctry`)<br>3. *SNTP, Schedule & OTA Services (9.4.5)* (Đồng bộ thời gian chuẩn, lịch trình offline độc lập mạng, OTA rollback an toàn)<br>4. *Physical Button Gesture Uplink (9.4.6)* (Nút Boot đa cử chỉ điều khiển LED & lập tức báo cáo trạng thái lên đám mây) |
| **ADR-007** | **Kiến Trúc Nâng Cấp Firmware Từ Xa OTA, Chẩn Đoán Sức Khỏe Phần Cứng & Tự Động Rollback Chống Brick** | [ADR-007.md](../ADR-007-ota-rollback-and-diagnostics.md) | [**Xem Sơ Đồ HTML**](./ADR-007-ota-rollback-and-diagnostics.html)<br>([JSON](./ADR-007-ota-rollback-and-diagnostics.architecture.json)) | 1. *OTA Using Parameters Workflow (11.3.1)* (URL tải trực tiếp qua CLI/API)<br>2. *OTA Using MQTT Topics Workflow (11.3.2)* (Chiến dịch phân phối Dashboard)<br>3. *Post-OTA Diagnostic & Health Evaluation* (Kiểm tra xung WS2812B & NVS Flash trước khi hủy rollback)<br>4. *Bootloader Auto-Rollback on Crash / Failure* (Tự động phục hồi slot cũ an toàn khi crash) |
| **ADR-008** | **Quản Lý Nguồn Điện Thông Minh: DFS, Automatic Light-sleep & Khóa PM Cho Driver LED WS2812B** | [ADR-008.md](../ADR-008-power-management-dfs-light-sleep.md) | [**Xem Sơ Đồ HTML**](./ADR-008-power-management-dfs-light-sleep.html)<br>([JSON](./ADR-008-power-management-dfs-light-sleep.architecture.json)) | 1. *Light ON: PM Lock Held & DFS Active* (Giữ khóa `ESP_PM_NO_LIGHT_SLEEP`, cấm ngủ để bảo vệ xung APB cho WS2812B DMA)<br>2. *Light OFF: Automatic Light-sleep (< 2-5mA)* (Nhả khóa, FreeRTOS Tickless Idle đưa chip vào Light-sleep đạt chuẩn Energy Star)<br>3. *Instantaneous Boot Button GPIO Wakeup* (Ngắt mức thấp GPIO đánh thức chip trong < 1 ms và bật đèn tức thì)<br>4. *Wi-Fi Modem-sleep & DTIM Coexistence* (Modem ngủ giữa các chu kỳ DTIM beacon, duy trì kết nối đám mây RainMaker) |
| **ADR-009** | **Bảo Mật Phần Cứng (Secure Boot v2, Flash Encryption) & Dây Chuyền Nạp Hàng Loạt** | [ADR-009.md](../ADR-009-security-and-mass-manufacturing.md) | [**Xem Sơ Đồ HTML**](./ADR-009-security-and-mass-manufacturing.html)<br>([JSON](./ADR-009-security-and-mass-manufacturing.architecture.json)) | 1. *Mass Manufacturing Data Pipeline (Chapter 14)* (Đóng gói chứng chỉ X.509, private key vào phân vùng NVS 24KB)<br>2. *Secure Boot v2 Cryptographic Chain (Chapter 13)* (Khóa ký RSA-3072, Public Digest lưu eFuse BLOCK_KEY0)<br>3. *Hardware AES-XTS Flash Encryption (Chapter 13)* (Khóa AES-256 eFuse BLOCK_KEY1 mã hóa bus SPI Flash ngoài)<br>4. *Assembly Line Flashing & Runtime Readout* (Tự động tìm offset 0x3E0000, nạp đè qua esptool, firmware đọc số sê-ri khi boot) |



---

## 🔍 Chi Tiết Sơ Đồ ADR-005 (Điều Khiển Cục Bộ Kênh Đôi)

Sơ đồ [`ADR-005-local-control-https-mdns-ble.html`](./ADR-005-local-control-https-mdns-ble.html) mô tả kiến trúc điều khiển cục bộ kênh đôi độc lập Internet/Cloud cho **Chương 8 (`test_case/local_control`)**:

```text
+-------------------------------------------------------------------------------------------------------------------------+
|                                    SƠ ĐỒ KIẾN TRÚC TỔNG THỂ ADR-005 (DUAL-CHANNEL ARCHITECTURE)                         |
+------------------------------+------------------------------+-----------------------------+-----------------------------+
| 1. CLIENT CONTROLLERS        | 2. PROTOCOL & SECURITY CORE  | 3. LOCAL CTRL ENGINE        | 4. HARDWARE HAL & LED EXEC  |
+------------------------------+------------------------------+-----------------------------+-----------------------------+
| [ LAN Client / Python Script] ---> [ mDNS Service Discovery] |                             |                             |
| • my_esp_ctrl_device.local   | • my_esp_ctrl_device.local   |                             |                             |
|              |               |              |               |                             |                             |
|              v               |              v               |                             |                             |
| [ HTTPS REST Client ] ---------> [ HTTPS Server (Port 443) ] --> [ esp_local_ctrl Engine ]  |                             |
| • GET /version, GET /control | • TLS X.509 cacert & prvtkey | • HTTPD Transport & Dispatch|                             |
| • POST /control {"status"}   |                              |              |              |                             |
|                              |                              |              v              |                             |
|                              |                              | [ Property 'status' ] ------> [ app_driver Core ]           |
|                              |                              | • JSON {"status": bool}     | • Đồng bộ trạng thái        |
|                              |                              |                             | • Nháy xung LED xác nhận    |
| [ BLE Smartphone ] ----------> [ BLE GATT Server ] ----------------------------------------->              |              |
| • App nRF Connect            | • Service UUID: 0x00FF       |                             |              v              |
| • Write 0x01 (Bật) / 0x00(Tắt| • Char 0x0001 (Write)        |                             | [ light_driver Engine ]     |
| • Không cần router Wi-Fi!    | • Char 0x0002 (Read)         |                             | • Hardware SPI2 DMA @ 3.2MHz|
|                              |                              |                             |              |              |
|                              |                              | [ Boot Button HAL ] ------> |              v              |
|                              |                              | • S3 GPIO 0 / C3 GPIO 9     | [ WS2812B 8-Bit Strip ]     |
|                              |                              | • Single/Double/Long Click  | • DIN GPIO 4 NeoPixel       |
+------------------------------+------------------------------+-----------------------------+-----------------------------+
```

### 4 Góc nhìn chuyên đề (Curated Interactive Views):
1. **Wi-Fi HTTPS & mDNS Control (8.5.1)**:
   - Dòng chảy từ LAN client phân giải tên miền mDNS `my_esp_ctrl_device.local`, bắt tay TLS bảo mật trên cổng 443 và cập nhật thuộc tính đèn qua giao thức REST.
2. **Fallback Bluetooth LE GATT (8.5.3)**:
   - Dòng điều khiển cự ly gần dự phòng khi router Wi-Fi bị cúp điện; smartphone kết nối BLE tới `ESP32-LOCAL-LIGHT`, ghi mã lệnh `0x01`/`0x00` vào Characteristic `0x0001` để bật/tắt đèn tức thì.
3. **Python Client Script Verification (8.5.2)**:
   - Quy trình kiểm thử tự động của script [`scripts/test_local_control.py`](file:///d:/Document/PBL5_stevejobvn/scripts/test_local_control.py) xác thực các endpoint `/version`, `/control` (GET) và gửi lệnh POST cập nhật trạng thái đèn.
4. **Hardware HAL & Visual Status**:
   - Tầng điều phối phần cứng `app_driver` phát xung định thời nano-giây xuống thanh LED WS2812B 8 hạt qua Hardware SPI2 DMA @ 3.2MHz trên GPIO 4 và xử lý ngắt nút bấm Boot vật lý đa cử chỉ.

---

## 🔍 Chi Tiết Sơ Đồ ADR-006 (ESP RainMaker Đám Mây & Thiết Bị Bóng)

Sơ đồ [`ADR-006-esp-rainmaker-cloud-control.html`](./ADR-006-esp-rainmaker-cloud-control.html) mô tả toàn cảnh kiến trúc điều khiển đám mây ESP RainMaker, luồng chứng thực Assisted Claiming và đồng bộ hóa hai chiều (Two-Way Device Shadow) cho **Chương 9 (`device_firmware/5_rainmaker`)**:

```text
+-------------------------------------------------------------------------------------------------------------------------+
|                                    SƠ ĐỒ KIẾN TRÚC TỔNG THỂ ADR-006 (RAINMAKER CLOUD & DEVICE SHADOW)                   |
+------------------------------+------------------------------+-----------------------------+-----------------------------+
| 1. CLIENT & CLOUD DOMAIN     | 2. INGESTION & TRANSPORT     | 3. EMBEDDED NODE & SHADOW   | 4. HARDWARE HAL & LED EXEC  |
+------------------------------+------------------------------+-----------------------------+-----------------------------+
| [ RainMaker Mobile App ] ----> [ BLE Provisioning (NimBLE) ]-> [ RainMaker Node Core ] ------> [ app_driver HAL ]          |
| • iOS / Android App          | • Assisted Claiming Handshake| • Lightbulb Device Node     | • Nút Boot đa cử chỉ        |
| • Quét mã QR BLE             | • Cấp phát Wi-Fi STA         | • Callback write_cb()       | • Đồng bộ LED & Cloud Shadow|
|              |               |                              |              |              |              |              |
|              v               | [ LAN Local Control ] ------->              v              |              v              |
| [ AWS IoT Core Cloud ] ------> • mDNS HTTPS Cục bộ          | [ Device Shadow (TSL) ] <---+ [ light_driver Engine ]     |
| • MQTT Mutual TLS Port 8883  |                              | • Power, Brightness,        | • Hardware SPI2 DMA @ 3.2MHz|
| • AWS Serverless Broker      | [ MQTT TLS Client ] ---------> • Hue, Saturation (HSV)       |              |              |
| • Quản lý chứng chỉ X.509    | • Trao đổi khóa an toàn      |              ^              |              v              |
|                              | • Phân vùng 'fctry' NVS      | [ Cloud Services Engine ] --+ [ WS2812B 8-Bit NeoPixel ]   |
|                              |                              | • SNTP Time Sync & Timezone | • DIN GPIO 4 NeoPixel       |
|                              |                              | • Offline Scheduling Service|                             |
|                              |                              | • OTA Upgrade Rollback Safe |                             |
|                              |                              | • Remote System Reset / Fcty|                             |
+------------------------------+------------------------------+-----------------------------+-----------------------------+
```

### 4 Góc nhìn chuyên đề (Curated Interactive Views):
1. **Cloud MQTT & Device Shadow Downlink (9.4.1)**:
   - Dòng lệnh điều khiển từ đám mây AWS IoT Core truyền qua MQTT TLS tới lõi RainMaker, giải mã các tham số TSL (Bật/Tắt, Độ sáng, Màu Hue, Độ bão hòa Saturation) và đồng bộ xuống phần cứng LED WS2812B.
2. **BLE Assisted Claiming & Provisioning (9.4.2)**:
   - Quy trình nạp chứng chỉ đám mây không cần nạp thủ công từ PC: App điện thoại kết nối Bluetooth LE NimBLE, gửi mã xác thực Claiming, ESP32 tạo cặp khóa và tải chứng chỉ thiết bị TLS X.509 lưu vào phân vùng bảo mật `fctry`.
3. **SNTP, Schedule & OTA Services (9.4.5)**:
   - Hệ sinh thái dịch vụ tích hợp sẵn: Tự đồng bộ giờ thực chuẩn xác qua SNTP `pool.ntp.org`, kích hoạt lịch trình hẹn giờ offline (vẫn chạy đúng giờ ngay cả khi mất mạng Wi-Fi/Internet), và kiểm soát nâng cấp firmware OTA an toàn có cơ chế chống brick.
4. **Physical Button Gesture Uplink (9.4.6)**:
   - Dòng phản hồi ngược từ thao tác vật lý: Khi người dùng bấm nút Boot trên mạch (nhấp đơn đổi Bật/Tắt, nhấp đúp xoay vòng bảng màu HSV), driver lập tức đổi màu LED và phát lệnh `esp_rmaker_param_update_and_report` để cập nhật bóng thiết bị trên điện thoại trong mili-giây.

---

## 🔍 Chi Tiết Sơ Đồ ADR-004 (Cập Nhật Toàn Diện)

Sơ đồ [`ADR-004-wifi-connection-and-visual-feedback.html`](./ADR-004-wifi-connection-and-visual-feedback.html) mô tả kiến trúc tầng mạng không dây cho cả hai chương **Chương 3 (`3_wifi_connection`)** và **Chương 4 (`4_network_config`)**:

```text
+-------------------------------------------------------------------------------------------------------------------------+
|                                    SƠ ĐỒ KIẾN TRÚC TỔNG THỂ ADR-004 (ARCHIFY SHOWCASE)                                  |
+------------------------------+------------------------------+-----------------------------+-----------------------------+
| 1. PROVISIONING & CREDENTIALS| 2. APPLICATION CORE & STORAGE| 3. ESP-IDF WIRELESS CORE    | 4. VISUAL & HARDWARE LAYER  |
+------------------------------+------------------------------+-----------------------------+-----------------------------+
| [ Mobile Application Matrix ]|                              |                             |                             |
| • ESP BLE Prov (Chương 4)    |                              |                             |                             |
| • ESP RainMaker (Chương 5-7) |                              |                             |                             |
|              |               |                              |                             |                             |
|              v               |                              |                             |                             |
| [ BLE Prov & Security 1 ] ---> [ Application Core ] ----------> [ esp_wifi STA Core ] ------> [ esp_event Loop ]       |
| • PoP: abcd1234              | • wifi_prov_mgr Supervisor   | • WPA2/WPA3 Handshake       | • STA_START / GOT_IP / FAIL |
| • Quét mã QR ASCII           | • Tách biệt State Machine    | • Tự động kết nối lại 5 lần |              |              |
|                              |              |               |              |              |              v              |
| [ Kconfig menuconfig ] ------>              |               |              v              | [ app_driver Dispatcher ]   |
| • Cấu hình tĩnh (Chương 3)   |              v               | [ Wi-Fi Router (AP) ]       | • Điều khiển màu trạng thái |
|                              | [ NVS Flash Storage ]        | • Gateway & Cấp phát DHCP   |              |              |
|                              | • Tự kết nối lại sau 1.4s    |                             |              v              |
|                              | • Bền vững khi khởi động lại |                             | [ light_driver Engine ]     |
|                              |                              | [ Boot Button HAL ] ------> | • Hardware SPI2 DMA @ 3.2MHz|
|                              |                              | • GPIO 0 (S3) / GPIO 9 (C3) |              |              |
|                              |                              |                             |              v              |
|                              |                              |                             | [ WS2812B 8-Bit Strip ]     |
|                              |                              |                             | • Cyan: Đang phát sóng BLE  |
|                              |                              |                             | • Vàng: Đang kết nối Router |
|                              |                              |                             | • Xanh lá: Đã có IP DHCP    |
|                              |                              |                             | • Đỏ: Sai mật khẩu / Lỗi    |
+------------------------------+------------------------------+-----------------------------+-----------------------------+
```

### 4 Góc nhìn chuyên đề (Curated Interactive Views):
1. **Wi-Fi Station & Kconfig (Ch 3)**:
   - Dòng chảy kết nối từ Kconfig `CONFIG_ESP_WIFI_SSID` / `PASSWORD` vào `app_main`, kích hoạt `wifi_sta` bắt tay với Router và đồng bộ hóa qua FreeRTOS Event Groups.
2. **BLE Smart Provisioning & QR Code (Ch 4)**:
   - Dòng chảy từ Smartphone quét mã QR ASCII, bắt tay bảo mật `Security 1` với mã PoP `abcd1234`, cấp phát thông tin mạng vào Flash NVS và tự động giải phóng vùng nhớ Bluetooth LE (`wifi_prov_mgr_deinit()`).
3. **Visual LED Feedback & Hardware Controls**:
   - Dòng phản hồi trạng thái mạng tức thời qua 4 màu LED (Thở Xanh Cyan, Thở Vàng, Sáng Xanh Lá, Bật Đỏ cảnh báo) trên thanh WS2812B và các cử chỉ nút Boot vật lý.
4. **Mobile App Architecture Boundary (Local vs Cloud)**:
   - Phân định rõ ràng: Chương 4 dùng **ESP BLE Provisioning** (cấp phát nội bộ với 4 endpoint GATT chuẩn); Chương 5, 6, 7 dùng **ESP RainMaker** (kèm endpoint `cloud_user_assoc`, phân vùng chứng chỉ `fctry` và đám mây AWS IoT).

---

## 💻 Hướng Dẫn Mở & Tương Tác Sơ Đồ

1. **Mở trực tiếp bằng Trình duyệt**:
   - Nhấp đúp chuột vào tệp `.html` mong muốn (ví dụ: [`ADR-004-wifi-connection-and-visual-feedback.html`](./ADR-004-wifi-connection-and-visual-feedback.html)).
   - Tệp sẽ tự động mở trên trình duyệt mặc định với đầy đủ tính năng.
2. **Các tính năng tương tác có sẵn trên thanh điều khiển**:
   - **Views (Góc nhìn)**: Chọn các tab góc nhìn ở góc trên để làm nổi bật luồng dữ liệu tương ứng.
   - **Theme**: Chuyển đổi linh hoạt giữa giao diện Tối (Dark) và Sáng (Light).
   - **Trace Motion**: Bật/Tắt hiệu ứng dòng chảy tín hiệu chạy dọc theo các mũi tên liên kết.
   - **Pan / Zoom**: Dùng chuột cuộn hoặc kéo để phóng to/thu nhỏ và di chuyển khung hình tự do.
   - **Search & Focus**: Gõ tên linh kiện để định vị và làm nổi bật tức thì trên sơ đồ.
