# ADR-007: Kiến Trúc Nâng Cấp Firmware Từ Xa OTA, Chẩn Đoán Sức Khỏe Phần Cứng & Tự Động Rollback Chống Brick

- **Trạng thái**: Đã phê duyệt (Accepted)
- **Ngày quyết định**: 2026-09-23
- **Phạm vi**: `device_firmware/5_rainmaker`, `test_case/advanced_https_ota`, `scripts/test_ota_server.py`, `docs/progress/11.3 Practice - Over-the-air (OTA) Example.md`
- **Sơ đồ kiến trúc tương tác (Archify)**: [Xem sơ đồ trực quan Showcase (HTML)](./diagrams/ADR-007-ota-rollback-and-diagnostics.html)

---

## Bối Cảnh & Vấn Đề (Context & Problem Statement)

Sau khi hoàn thành tính năng điều khiển đám mây từ xa qua ESP RainMaker ở Chương 9 theo [ADR-006](./ADR-006-esp-rainmaker-cloud-control.md), một trong những yêu cầu sống còn của thiết bị IoT thương mại là khả năng cập nhật firmware từ xa qua mạng Wi-Fi (Over-the-Air - OTA) theo **Mục 11.3 Practice: Over-the-air (OTA) Example**.

Quá trình nâng cấp firmware từ xa luôn tiềm ẩn rủi ro rất cao đối với thiết bị nhúng đặt tại nhà người dùng:
1. **Nguy cơ Thiết bị Biến Thành "Cục Gạch" (Device Bricking Risk)**: Nếu bản firmware mới tải về bị lỗi logic, crash vòng lặp (panic/boot loop), không tương thích phần cứng, hoặc lỗi kết nối mạng khiến chip không thể duy trì kết nối Internet, thiết bị sẽ bị ngắt kết nối hoàn toàn và không thể tiếp tục nhận bản vá sửa lỗi nếu không có cáp UART nạp lại trực tiếp.
2. **Xác thực Tính Toàn Vẹn & Phần Cứng Sau Cập Nhật (Post-OTA Hardware Sanity)**: Làm thế nào để firmware mới tự kiểm tra xem driver phần cứng ngoại vi (LED WS2812B Hardware SPI2 DMA @ 3.2MHz trên GPIO 4) và phân vùng lưu trữ NVS Flash có hoạt động bình thường hay không trước khi quyết định "chốt" phiên bản mới?
3. **Đa Dạng Phương Thức Triển Khai (Flexible Deployment Workflows)**:
   - Trong quá trình nghiên cứu & phát triển (R&D): Kỹ sư cần kiểm thử nhanh bằng cách truyền URL tải firmware từ máy chủ nội bộ thông qua RainMaker CLI hoặc API (`OTA_USING_PARAMS`).
   - Trong giai đoạn vận hành thương mại: Nhà quản trị cần phát hành chiến dịch cập nhật hàng loạt cho hàng nghìn thiết bị thông qua ESP RainMaker Cloud Dashboard (`OTA_USING_TOPICS`).
4. **Hạ Tầng Phân Phối Firmware Cục Bộ (Local OTA Streaming)**: Trình khách OTA của ESP-IDF (`esp_https_ota`) hỗ trợ tải luồng dữ liệu từng đoạn (chunked transfer) và Range requests (HTTP 206 Partial Content). Cần một máy chủ thử nghiệm cục bộ chuẩn mực giúp nhà phát triển dễ dàng kiểm thử tính năng nạp mà không phụ thuộc vào hạ tầng cloud hosting bên thứ ba.

---

## 1. Quyết Định Kiến Trúc Tổng Thể

### 1.1. Cơ Chế Tự Động Rollback Kép (Dual Partition Rollback Mechanism)
Bảo vệ tuyệt đối cho thiết bị bằng cách phối hợp giữa **2nd Stage Bootloader** và **Bảng Phân Vùng Song Song**:
- **Bảng phân vùng Flash 4MB (`partitions.csv`)**:
  - `otadata` (8KB tại offset `0x16000`): Lưu con trỏ trạng thái phân vùng hoạt động.
  - `ota_0` (1920KB tại offset `0x20000`): Vùng chứa firmware hiện tại (App Slot 0).
  - `ota_1` (1920KB tại offset `0x200000`): Vùng chứa firmware dự phòng / nâng cấp (App Slot 1).
- **Cấu hình Bootloader**:
  - Bật `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`.
- **Máy trạng thái phân vùng OTA (OTA Partition State Machine)**:
  1. Khi nạp xong bản vá vào phân vùng đối ứng (ví dụ `ota_1`), bootloader đánh dấu trạng thái phân vùng là `ESP_OTA_IMG_PENDING_VERIFY`.
  2. Thiết bị tự khởi động lại vào `ota_1`.
  3. Nếu trong quá trình chạy thử nghiệm, firmware mới bị crash / reset liên tục hoặc thất bại ở bước kiểm tra chẩn đoán, bootloader sẽ tự động đánh dấu phân vùng mới là `ESP_OTA_IMG_INVALID` và chuyển quyền boot trở lại phân vùng cũ `ota_0`.
  4. Chỉ khi firmware mới thực thi thành công toàn bộ bài kiểm tra chẩn đoán và gọi hàm `esp_ota_mark_app_valid_cancel_rollback()`, phân vùng mới mới chính thức trở thành `ESP_OTA_IMG_VALID`.

```text
+---------------------------------------------------------------------------------------------------+
|                        MÁY TRẠNG THÁI KIỂM ĐỊNH ROLLBACK PHÂN VÙNG OTA                            |
+---------------------------------------------------------------------------------------------------+
|                                                                                                   |
|    [Bắt đầu tải OTA]                                                                              |
|           |                                                                                       |
|           v                                                                                       |
|    [Ghi Flash Slot đối ứng]                                                                        |
|           |                                                                                       |
|           v                                                                                       |
|    [Reboot Thiết bị] ---> Bootloader đặt trạng thái: ESP_OTA_IMG_PENDING_VERIFY                   |
|                                     |                                                             |
|                                     v                                                             |
|                          +---------------------+                                                  |
|                          | app_ota_diagnostic  |                                                  |
|                          +---------------------+                                                  |
|                                     |                                                             |
|                    +----------------+----------------+                                            |
|                    |                                 |                                            |
|             (Pha 1 & 2 PASSED)             (Crash / Lỗi Phần Cứng)                                |
|                    |                                 |                                            |
|                    v                                 v                                            |
|    +-------------------------------+   +------------------------------------+                     |
|    | esp_ota_mark_app_valid...()   |   | Bootloader phát hiện boot fail     |                     |
|    | Trạng thái: ESP_OTA_IMG_VALID |   | Đánh dấu: ESP_OTA_IMG_INVALID      |                     |
|    | HỦY ROLLBACK THÀNH CÔNG       |   | TỰ ĐỘNG BOOT LẠI PHÂN VÙNG CŨ      |                     |
|    +-------------------------------+   +------------------------------------+                     |
|                                                                                                   |
+---------------------------------------------------------------------------------------------------+
```

---

### 1.2. Hàm Chẩn Đoán Sức Khỏe Hai Pha (`app_ota_diagnostic`)
Tuân thủ nguyên mẫu hàm chính thức của ESP RainMaker Core (`esp_rmaker_post_ota_diag_t`):
```c
static esp_rmaker_ota_diag_status_t app_ota_diagnostic(esp_rmaker_ota_diag_priv_t *ota_diag_priv, void *priv);
```
Quy trình chẩn đoán được chia làm 2 pha tuần tự chặt chẽ:
1. **Pha 1 — Kiểm tra Phần Cứng Cục Bộ (`OTA_DIAG_STATE_INIT`)**:
   - Được gọi ngay khi `esp_rmaker_ota_enable()` khởi chạy trên phân vùng mới.
   - Kiểm tra khả năng điều khiển của driver WS2812B: đọc trạng thái hiện tại, phát xung nhấp nháy đèn xanh lá trong 150ms để kiểm chứng ngoại vi SPI2 DMA không bị kẹt hoặc mất xung.
   - Kiểm tra tính toàn vẹn của phân vùng NVS Flash (`app_storage_init()`).
   - Nếu có bất kỳ lỗi nào, trả về `OTA_DIAG_STATUS_FAIL` để ép thiết bị reboot rollback ngay lập tức. Nếu đạt, trả về `OTA_DIAG_STATUS_SUCCESS`.
2. **Pha 2 — Kiểm tra Kết Nối Đám Mây (`OTA_DIAG_STATE_POST_MQTT`)**:
   - Được gọi sau khi modem Wi-Fi đã bắt tay TLS và thiết lập kết nối MQTT ổn định với AWS IoT Core của ESP RainMaker.
   - Trả về `OTA_DIAG_STATUS_SUCCESS`, cho phép RainMaker Core gọi `esp_ota_mark_app_valid_cancel_rollback()` và báo cáo trạng thái `OTA_STATUS_SUCCESS` lên Cloud Dashboard.

---

### 1.3. Cấu Hình Kép: Kích Hoạt OTA Qua Tham Số Hoặc Qua Đám Mây
Để đáp ứng cả hai môi trường phát triển và thương mại:
- Thiết lập tệp cấu hình `main/Kconfig.projbuild` cung cấp menu lựa chọn chế độ OTA:
  - `CONFIG_APP_OTA_USING_PARAMS`: Sử dụng URL tham số (`OTA_USING_PARAMS`) cho phép kỹ sư truyền link trực tiếp từ RainMaker CLI.
  - `CONFIG_APP_OTA_USING_TOPICS`: Sử dụng tác vụ MQTT Topic tập trung (`OTA_USING_TOPICS`) do RainMaker Dashboard phân phối.
- Gắn callback chẩn đoán thống nhất vào cấu trúc cấu hình:
  ```c
  esp_rmaker_ota_config_t ota_config = {
      .server_cert = ota_server_cert,
      .ota_diag = app_ota_diagnostic,
  };
  #if defined(CONFIG_APP_OTA_USING_TOPICS)
      esp_rmaker_ota_enable(&ota_config, OTA_USING_TOPICS);
  #else
      esp_rmaker_ota_enable(&ota_config, OTA_USING_PARAMS);
  #endif
  ```

---

### 1.4. Máy Chủ Thử Nghiệm OTA Cục Bộ (`scripts/test_ota_server.py`)
Phát triển một công cụ phục vụ phát triển & kiểm thử nội bộ:
- Dựa trên nền tảng Python `http.server.ThreadingHTTPServer` với custom `OTARequestHandler`.
- **Hỗ trợ Range Requests (HTTP 206 Partial Content)**: Xử lý chính xác header `Range: bytes=start-end` và `Content-Range`, đảm bảo tương thích 100% với cơ chế tải luồng từng đoạn của ESP-IDF OTA Client.
- **Tự Động Nhận Diện IP Mạng LAN**: Quét danh sách giao diện mạng IPv4 thực tế của máy tính lập trình viên, hiển thị đường dẫn URL chính xác.
- **Tính Toán Bảng Băm Checksum**: Tự động sinh mã băm SHA256 và MD5 cho mọi tệp nhị phân `.bin` trong thư mục, giúp kiểm chứng nhanh với thông tin log trên vi điều khiển.
- **Tạo Cú Pháp Lệnh CLI Sẵn Sàng**: In sẵn câu lệnh RainMaker CLI và curl tương ứng.

---

## 2. Hệ Quả & Đánh Giá Kiến Trúc (Consequences)

### 2.1. Điểm Tích Cực
- **An toàn tuyệt đối (Zero Bricking Risk)**: Cơ chế Bootloader Rollback bảo đảm thiết bị luôn tự động phục hồi về phiên bản trước nếu bản cập nhật gặp sự cố chết người.
- **Độ tin cậy phần cứng cao (High Peripheral Reliability)**: Bài test chẩn đoán 2 pha bảo đảm đèn WS2812B và bộ nhớ Flash hoạt động ổn định trước khi xác nhận bản vá mới.
- **Tiện ích phát triển vượt trội (Superior DX)**: Script `test_ota_server.py` giúp toàn bộ chu trình build $\rightarrow$ host $\rightarrow$ upgrade $\rightarrow$ verify diễn ra chỉ trong vài giây ngay trên mạng nội bộ.
- **Tuân thủ phân lớp kế thừa**: Bảo tồn nguyên vẹn các thành phần trong `device_firmware/components/` và tương thích 100% trên cả 2 dòng chip ESP32-S3 và ESP32-C3.

### 2.2. Điểm Cần Lưu Ý
- **Dung lượng Flash phân vùng**: Mỗi slot OTA chiếm 1920KB (tổng cộng 3840KB trên Flash 4MB), để lại 128KB cho NVS và phân vùng hệ thống. Cần theo dõi kích thước nhị phân khi tích hợp thêm các tính năng ở các chương sau (Power Management, ESP Insights).
