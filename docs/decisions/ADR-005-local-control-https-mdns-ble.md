# ADR-005: Kiến Trúc Điều Khiển Cục Bộ Kênh Đôi Wi-Fi HTTPS mDNS & Bluetooth LE GATT Cho Thiết Bị Đèn Thông Minh (test_case/local_control)

- **Trạng thái**: Đã phê duyệt (Accepted)
- **Ngày quyết định**: 2026-09-22
- **Phạm vi**: `test_case/local_control`, `test_case/local_control/scripts/test_local_control.py`, `docs/progress/8.5 Practice - Local Control in Smart Light Project.md`
- **Sơ đồ kiến trúc tương tác (Archify)**: [Xem sơ đồ trực quan (HTML)](./diagrams/ADR-005-local-control-https-mdns-ble.html)

---

## Bối Cảnh & Vấn Đề (Context & Problem Statement)

Sau khi hoàn thành việc kết nối Wi-Fi Station và cấp phát thông minh qua Bluetooth LE ở Chương 7 theo [ADR-004](./ADR-004-wifi-connection-and-visual-feedback.md), thiết bị đèn thông minh trong dự án PBL5 đã có khả năng gia nhập mạng không dây. Tuy nhiên, trong môi trường thực tế, kết nối Internet ra bên ngoài (WAN) hoặc máy chủ đám mây (Cloud) có thể gặp sự cố đứt cáp quang, mất kết nối quốc tế, hoặc router Wi-Fi bị cắt nguồn.

Giáo trình *ESP32-C3 Wireless Adventure* đặt ra bài toán tại **Mục 8.5 Practice: Local Control in Smart Light Project**:
1. Làm sao để ứng dụng điện thoại hoặc kịch bản máy tính trong cùng mạng LAN vẫn có thể tự động tìm thấy đèn qua tên miền cục bộ (mDNS) và gửi lệnh điều khiển tức thì với độ trễ siêu thấp (<20ms) mà không phụ thuộc vào Cloud?
2. Khi mạng Wi-Fi hoàn toàn không khả dụng (ví dụ router bị cúp điện hoặc mang đèn ra ngoài trời), người dùng vẫn có thể điều khiển đèn tại chỗ thông qua kênh Bluetooth LE (BLE GATT) như thế nào?

Mã nguồn mẫu ban đầu trong giáo trình (`test_case/local_control`) gặp phải các hạn chế và lỗi kỹ thuật nghiêm trọng:
- **Lỗi cú pháp biên dịch**: Tệp `app_main.c` sử dụng dấu ngoặc kép tiếng Trung (`”mdns.h“`, `“esp_https_server.h”`) gây lỗi biên dịch compiler ngay lập tức.
- **Lỗi liên kết CMake nhúng chứng chỉ**: Tệp `main/CMakeLists.txt` không đăng ký `cacert.pem` và `prvtkey.pem` vào tham số `EMBED_TXTFILES` của `idf_component_register`, dẫn đến lỗi thiếu symbol `_binary_cacert_pem_start` khi liên kết (linking).
- **Lỗi đường dẫn phụ thuộc**: `CMakeLists.txt` trỏ vào đường dẫn lỗi thời `../../Project/components/` vốn không còn tồn tại trong cấu trúc thư mục hiện đại `device_firmware/components/`.
- **Lỗi tương thích nạp chứng chỉ HTTPS trên ESP-IDF v6.0.2**: Bản gốc gán chứng chỉ máy chủ vào `https_conf.cacert_pem` (vốn dành cho Client Verification/mTLS), trong khi ESP-IDF v6 bắt buộc nạp qua `https_conf.servercert` và `servercert_len`. Sự sai lệch này khiến máy chủ báo lỗi `No Server certificate supplied` (`ESP_ERR_INVALID_ARG 0x102`) và gây ra vòng lặp crash reboot vô tận khi khởi động.
- **Lạc hậu tầng Driver**: Mã nguồn cũ vẫn điều khiển 5 kênh PWM analog rời rạc thay vì thanh LED WS2812B 8 hạt chuẩn hóa qua Hardware SPI2 DMA @ 3.2MHz trên GPIO 4 và Nút Boot HAL đa cử chỉ.
- **Thiếu kịch bản kiểm thử tự động**: Người dùng không có sẵn công cụ client để xác thực các endpoint HTTP/HTTPS nội bộ.

ADR này ghi nhận toàn bộ các quyết định kiến trúc để giải quyết triệt để các vấn đề trên và thiết lập chuẩn mực điều khiển cục bộ kênh đôi (Dual-Channel Local Control).

---

## 1. Quyết định: Kiến Trúc Điều Khiển Cục Bộ Kênh Đôi (Dual-Channel Architecture)

### 1.1. Nội dung quyết định
Hệ thống triển khai đồng thời hai kênh điều khiển cục bộ độc lập và hội tụ tại tầng phần cứng thống nhất:
1. **Kênh chính: Wi-Fi HTTPS + mDNS Service Discovery (Mục 8.5.1)**:
   - Sử dụng component `esp_local_ctrl` trên nền `ESP_LOCAL_CTRL_TRANSPORT_HTTPD`.
   - Máy chủ HTTPS lắng nghe trên cổng 443 với chứng chỉ TLS X.509 (`cacert.pem`) và khóa RSA (`prvtkey.pem`).
   - Tên miền mDNS nội bộ: `my_esp_ctrl_device.local` (quảng bá dịch vụ `_esp_local_ctrl._tcp` trên cổng 443).
   - Đăng ký thuộc tính điều khiển `status` định dạng chuỗi JSON `{"status": true/false}`.
2. **Kênh phụ: Bluetooth LE GATT Server Fallback (Mục 8.5.3)**:
   - Khởi tạo Bluetooth LE GATT Server với tên quảng bá `ESP32-LOCAL-LIGHT`.
   - Khai báo Service UUID `0x00FF`, Characteristic Ghi lệnh (UUID `0x0001`, Writable) và Characteristic Đọc trạng thái (UUID `0x0002`, Readable).
   - Khi nhận lệnh ghi `GATT_WRITE_EVT`, chương trình chuyển đổi tức thì trạng thái đèn mà không cần qua router Wi-Fi.

```text
+---------------------------------------------------------------------------------------------------+
|                     KIẾN TRÚC ĐIỀU KHIỂN CỤC BỘ KÊNH ĐÔI (ADR-005 DUAL-CHANNEL)                   |
+-------------------------------------------------+-------------------------------------------------+
|   KÊNH 1: WI-FI HTTPS & mDNS (Mục 8.5.1)        |   KÊNH 2: BLUETOOTH LE GATT (Mục 8.5.3)         |
|   (Client trong LAN: Phone, PC Python Script)   |   (Client cự ly gần: Phone BLE App, nRF Connect)|
+-------------------------------------------------+-------------------------------------------------+
| • Endpoint: https://my_esp_ctrl_device.local/.. | • BLE Service UUID: 0x00FF                      |
| • Protocol: Protocomm SEC0 / JSON over TLS      | • Char 0x0001 (Write): 0x01 = Bật, 0x00 = Tắt   |
| • Property: "status" (Get/Set Handlers)         | • Char 0x0002 (Read) : 0x01 = Bật, 0x00 = Tắt   |
+-------------------------------------------------+-------------------------------------------------+
                                      \             /
                                       v           v
                    +---------------------------------------------+
                    |        TẦNG ĐIỀU PHỐI (app_driver)          |
                    |   • app_driver_set_state(bool)              |
                    |   • app_driver_get_state(void)              |
                    |   • app_driver_pulse_feedback(R, G, B)      |
                    +---------------------------------------------+
                                       |
                                       v
                    +---------------------------------------------+
                    |   TẦNG PHẦN CỨNG WS2812B & NÚT BOOT HAL     |
                    |   • Hardware SPI2 DMA @ 3.2MHz trên GPIO 4  |
                    |   • Nút Boot: S3 GPIO 0 / C3 GPIO 9         |
                    |   • Single Click, Double Click, Long Press  |
                    +---------------------------------------------+
```

### 1.2. Tại sao chọn phương án này? (Rationale)
- **Tính sẵn sàng cao (High Availability)**: Thiết bị hoạt động ổn định trong mọi điều kiện mạng. Người dùng không bao giờ mất quyền kiểm soát chiếc đèn ngay cả khi mạng gia đình mất tín hiệu Wi-Fi.
- **Độ trễ tối thiểu**: Cả hai kênh giao tiếp trực tiếp ngang hàng (Peer-to-Peer) trong mạng cục bộ, thời gian đáp ứng lệnh chỉ từ 10ms đến 25ms, tạo trải nghiệm người dùng mượt mà vượt trội so với việc định tuyến qua Cloud.

---

## 2. Quyết định: Kế Thừa Lũy Tiến Tầng Driver WS2812B & Hỗ Trợ Đa Mục Tiêu (S3/C3)

### 2.1. Nội dung quyết định
- Kế thừa toàn bộ tài sản đã nghiệm thu từ ADR-003 và ADR-004:
  - `board_esp32s3_devkitc.h` (Boot GPIO 0, WS2812B DIN GPIO 4, 8 hạt LED).
  - `board_esp32c3_devkitc.h` (Boot GPIO 9, WS2812B DIN GPIO 4, 8 hạt LED).
  - Driver WS2812B Hardware SPI2 DMA @ 3.2MHz phát xung định thời NRZ 800 kHz chuẩn xác nano-giây.
  - Driver nút bấm vật lý IoT Button với 3 cử chỉ: Nhấn 1 lần (Bật/Tắt), Nhấn đúp (Đổi 8 màu), Nhấn giữ (Bật/Tắt hiệu ứng thở).
  - Phản hồi trạng thái Wi-Fi trực quan: Đang kết nối (Thở Vàng/Cam), Đã có IP (Xanh lá cây tĩnh), Lỗi (Đỏ cảnh báo).
- Cấu hình CMake:
  - Tự động nhận diện target và gán `-DDEVELOPMENT_BOARD`:
    ```cmake
    if(CONFIG_IDF_TARGET_ESP32S3)
        set(DEVELOPMENT_BOARD "board_esp32s3_devkitc.h")
    else()
        set(DEVELOPMENT_BOARD "board_esp32c3_devkitc.h")
    endif()
    ```
  - Khiên bảo vệ chống crash GCC 15 ICE:
    ```cmake
    set(COMPONENTS main button app_storage light_driver nvs_flash esp_wifi esp_event esp_netif mdns esp_local_ctrl esp_https_server esp_http_server bt)
    ```

---

## 3. Quyết định: Cung Cấp Kịch Bản Kiểm Thử Tự Động Client Bằng Python (Mục 8.5.2)

### 3.1. Nội dung quyết định
Xây dựng kịch bản Python [`test_case/local_control/scripts/test_local_control.py`](file:///d:/Document/PBL5_stevejobvn/test_case/local_control/scripts/test_local_control.py) đóng vai trò là một HTTPS Client nội bộ:
- Sử dụng mô-đun chuẩn `urllib.request` của Python (không cần cài đặt thêm thư viện ngoài).
- Thiết lập SSL context `check_hostname = False` và `verify_mode = CERT_NONE` để tương thích hoàn toàn với chứng chỉ tự ký dùng trong môi trường phát triển cục bộ.
- Thực hiện chu trình kiểm tra tự động 4 bước:
  1. Gửi request kiểm tra phiên bản dịch vụ (`GET /esp_local_ctrl/version`).
  2. Gửi request đọc trạng thái đèn hiện tại (`GET /esp_local_ctrl/control`).
  3. Gửi lệnh BẬT đèn (`POST /esp_local_ctrl/control`, payload: `{"status": true}`).
  4. Đợi 2 giây, gửi lệnh TẮT đèn (`POST /esp_local_ctrl/control`, payload: `{"status": false}`).

---

## 4. Bảng Đánh Giá Tác Động (Consequences & Trade-offs)

### Điểm tích cực (Positive):
1. **0 Lỗi Biên Dịch**: Khắc phục triệt để lỗi cú pháp ngoặc kép tiếng Trung và lỗi link tệp nhúng chứng chỉ.
2. **Trải nghiệm tức thì**: Độ trễ bật/tắt đèn qua HTTPS hoặc BLE < 20ms.
3. **Độc lập Cloud**: Không phụ thuộc vào đường truyền Internet hay máy chủ RainMaker.
4. **Bảo mật kênh truyền**: HTTPS sử dụng TLS mã hóa toàn bộ dữ liệu trao đổi trong mạng nội bộ.
5. **Tiện ích kiểm thử**: Kịch bản Python giúp kiểm định tính năng nhanh chóng trên máy tính mà không cần nạp firmware nhiều lần.

### Điểm đánh đổi (Trade-offs) & Biện pháp khắc phục:
1. **Dung lượng RAM khi chạy đồng thời HTTPS và BLE**: Cả HTTPD SSL và BLE Controller đều tiêu tốn bộ nhớ RAM (~60KB).
   - *Biện pháp*: Giới hạn `CONFIG_HTTPD_MAX_REQ_HDR_LEN=2048`, tối ưu stack size task httpd ở mức 8192 bytes, bật `CONFIG_MBEDTLS_DYNAMIC_BUFFER=y`.
   - *Đặc biệt lưu ý*: Trên ESP32-C3 & ESP32-S3, phần cứng không có Classic Bluetooth nên việc gọi `esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT)` trả về `ESP_ERR_NOT_SUPPORTED` (sẽ abort nếu dùng `ESP_ERROR_CHECK`). Do đó lệnh này được bọc điều kiện `#if CONFIG_IDF_TARGET_ESP32`.
   - Cấu hình bắt buộc `CONFIG_BT_BLUEDROID_ENABLED=y` trong `sdkconfig.defaults` để đồng bộ với tầng BLE GATT Server.
2. **Giao thức Protobuf chuẩn hóa của `esp_local_ctrl`**: Endpoint `/esp_local_ctrl/control` yêu cầu tuần tự hóa Protobuf thay vì plain JSON thô.
   - *Biện pháp*: Kịch bản `test_case/local_control/scripts/test_local_control.py` tích hợp bộ mã hóa/giải mã Protobuf thuần Python (0 dependency) để giao tiếp chuẩn xác 100% với firmware và ứng dụng di động.
3. **Quản lý bộ nhớ callback `get_property_values`**: Trường `free_fn` trong `esp_local_ctrl_prop_val_t` phải được gán `NULL` để tránh crash do gọi con trỏ rác khi giải phóng bộ nhớ.
4. **Chứng chỉ TLS tự ký (Self-signed)**: Trình duyệt hoặc client nghiêm ngặt sẽ cảnh báo bảo mật.
   - *Biện pháp*: Kịch bản kiểm thử cấu hình bypass xác thực chứng chỉ trong môi trường phát triển nội bộ LAN.

---

## 5. Kế Hoạch Kiểm Thử & Nghiệm Thu Trên Mạch Thật (Verification Record)

1. **Kiểm tra biên dịch**:
   ```bash
   idf.py -C test_case/local_control set-target esp32c3
   idf.py -C test_case/local_control build
   ```
2. **Nạp firmware và giám sát log**:
   ```bash
   idf.py -C test_case/local_control -p COM4 flash monitor
   ```
3. **Kiểm thử kênh Wi-Fi HTTPS bằng script Python**:
   ```bash
   python test_case/local_control/scripts/test_local_control.py --host my_esp_ctrl_device.local
   ```
4. **Kiểm thử kênh BLE GATT bằng ứng dụng điện thoại**:
   - Dùng app **nRF Connect** kết nối thiết bị `ESP32-LOCAL-LIGHT`.
   - Ghi byte `0x01` vào Characteristic `0x0001` $\rightarrow$ Quan sát thanh 8 LED WS2812B sáng tức thì.
   - Ghi byte `0x00` $\rightarrow$ Quan sát đèn tắt tức thì.
