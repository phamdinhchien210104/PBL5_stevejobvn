# ADR-004: Kiến Trúc Kết Nối Wi-Fi Station, Đồng Bộ FreeRTOS Event Groups và Phản Hồi Thị Giác Trên Thanh LED WS2812B (3_wifi_connection)

- **Trạng thái**: Đã phê duyệt (Accepted)
- **Ngày quyết định**: 2026-09-19
- **Phạm vi**: `device_firmware/3_wifi_connection`, `docs/progress/7.5 Practice - Wi-Fi Configuration in Smart Light Project.md`
- **Sơ đồ kiến trúc tương tác (Archify)**: [Xem sơ đồ trực quan (HTML)](./diagrams/ADR-004-wifi-connection-and-visual-feedback.html)

---

## Bối Cảnh & Vấn Đề (Context & Problem Statement)

Sau khi hoàn thiện tầng driver phần cứng cơ sở ở Chương 2 (**`2_light_drivers`**) theo [ADR-003](./ADR-003-light-drivers-and-button-modernization.md), dự án chuyển sang giai đoạn kết nối mạng không dây tại Chương 3 (**`device_firmware/3_wifi_connection`**), tương ứng với **Mục 7.5.1: Wi-Fi Connection** trong giáo trình *ESP32-C3 Wireless Adventure*.

Mã nguồn ban đầu của giáo trình gặp phải các hạn chế và xung đột kiến trúc thực tế:
1. **Lạc hậu tầng Driver LED**: Mã nguồn gốc gọi 5 kênh LED PWM analog cũ (`gpio_red`, `gpio_green`, `gpio_blue`, `gpio_cold`, `gpio_warm`), không tương thích với **Thanh LED WS2812B 8-Bit NeoPixel** đã được chuẩn hóa trong ADR-003.
2. **Thiếu phản hồi trạng thái mạng cho người dùng (Zero Visual Feedback)**: Khi chip dò tìm Wi-Fi, bắt tay 4 bước WPA2, hoặc mất kết nối, hệ thống chỉ in log chữ qua cổng Serial (UART). Trên thiết bị đèn thực tế không có màn hình OLED/LCD, người dùng không thể biết đèn đã vào mạng hay bị lỗi sai mật khẩu.
3. **Hardcode thông tin mạng gây mất an toàn**: Tên Wi-Fi (`SSID`) và Mật khẩu bị gán tĩnh trong mã nguồn C (`#define LIGHT_ESP_WIFI_SSID`), gây phiền phức khi chuyển đổi môi trường thử nghiệm và rủi ro lộ lọt mật khẩu lên Git.
4. **Thiếu hỗ trợ Đa Mục Tiêu (ESP32-S3 & ESP32-C3)**: Thư mục chỉ cấu hình riêng cho chip C3 (`board_esp32c3_devkitc.h`), CMakeLists chưa tự động nhận diện target và chưa có cơ chế bảo vệ trình biên dịch GCC 15 chống lỗi Internal Compiler Error (ICE).

ADR này ghi nhận toàn bộ các quyết định tái cấu trúc mã nguồn bên trong `device_firmware/3_wifi_connection/` để giải quyết triệt để các vấn đề trên.

---

## 1. Quyết định: Ghép Nối Tầng Driver WS2812B NeoPixel & Tích Hợp Hệ Thống Phản Hồi Thị Giác Trực Quan

### 1.1. Sửa, ghi thêm, cập nhật những gì?
- **Tại [`main/app_driver.c`](file:///d:/Document/PBL5_stevejobvn/device_firmware/3_wifi_connection/main/app_driver.c)**:
  - Khởi tạo `light_driver_config_t` với hai trường cấu hình chuẩn: `.gpio_ws2812 = LIGHT_WS2818_GPIO` (chân 4) và `.num_leds = LIGHT_WS2818_NUM_LEDS` (8 hạt LED).
  - Tầng driver tự động kích hoạt **Hardware SPI2 DMA @ 3.2MHz** để phát chuỗi xung định thời NRZ 800 kHz chuẩn xác nano-giây xuống thanh LED WS2812B.
  - Bổ sung hàm API chỉ báo thị giác:
    ```c
    void app_driver_set_wifi_status(wifi_status_t status)
    {
        switch (status) {
        case WIFI_STATUS_CONNECTING:
            // Đang kết nối / retry: Đèn chạy hiệu ứng Thở (Breathing) màu Vàng/Cam
            light_driver_set_switch(true);
            light_driver_breath_start(255, 160, 0);
            break;
        case WIFI_STATUS_CONNECTED:
            // Đã nhận IP từ Router: Dừng thở và bật màu Xanh lá cây
            light_driver_breath_stop();
            light_driver_set_switch(true);
            light_driver_set_rgb(0, 255, 0);
            break;
        case WIFI_STATUS_FAILED:
            // Thất bại sau 5 lần thử: Bật màu Đỏ cảnh báo
            light_driver_breath_stop();
            light_driver_set_switch(true);
            light_driver_set_rgb(255, 0, 0);
            break;
        }
    }
    ```
  - Mở rộng tương tác nút bấm Boot vật lý: Nhấn 1 lần (Bật/Tắt đèn), Nhấn đúp (Đổi 8 màu mẫu qua mảng `s_colors[]`), Nhấn giữ (Bật/Tắt hiệu ứng thở).
- **Tại [`main/include/app_priv.h`](file:///d:/Document/PBL5_stevejobvn/device_firmware/3_wifi_connection/main/include/app_priv.h)**:
  - Khai báo enum `wifi_status_t` (`WIFI_STATUS_CONNECTING`, `WIFI_STATUS_CONNECTED`, `WIFI_STATUS_FAILED`).
  - Khai báo nguyên mẫu hàm `void app_driver_set_wifi_status(wifi_status_t status);`.

### 1.2. Tại sao phải làm vậy? (Rationale)
1. **Thiết bị IoT không đầu (Headless Device)**: Đèn thông minh là sản phẩm không trang bị màn hình. Việc phản hồi trạng thái kết nối trực tiếp qua màu sắc thanh 8 LED giúp người dùng nắm bắt trạng thái hoạt động tức thời mà không cần công cụ debug chuyên dụng.
2. **Kế thừa thống nhất kiến trúc ADR-003**: Đảm bảo toàn bộ firmware sử dụng chung 1 bộ driver WS2812B SPI DMA duy nhất trong `components/light_driver`, loại bỏ hoàn toàn mã PWM analog cũ.

---

## 2. Quyết định: Đồng Bộ Hóa Bất Đồng Bộ Với FreeRTOS Event Groups & Auto-Reconnect

### 2.1. Sửa, ghi thêm, cập nhật những gì?
- **Tại [`main/app_main.c`](file:///d:/Document/PBL5_stevejobvn/device_firmware/3_wifi_connection/main/app_main.c)**:
  - Sử dụng FreeRTOS Event Group (`s_wifi_event_group = xEventGroupCreate()`) với 2 cờ sự kiện: `WIFI_CONNECTED_BIT` (Bit 0) và `WIFI_FAIL_BIT` (Bit 1).
  - Tác vụ `app_main` chuyển sang trạng thái Block (ngủ hoàn toàn, giải phóng 100% CPU) bằng `xEventGroupWaitBits()` chờ kết quả kết nối.
  - Bộ xử lý sự kiện mặc định (`event_handler`) tự động bắt các tín hiệu từ hệ thống mạng và gọi hàm phản hồi thị giác:
    - Bắt `WIFI_EVENT_STA_START` $\to$ Bắt đầu kết nối và chuyển đèn sang trạng thái `WIFI_STATUS_CONNECTING`.
    - Bắt `WIFI_EVENT_STA_DISCONNECTED` $\to$ Đếm số lần thử lại (`s_retry_num`). Nếu `s_retry_num < CONFIG_ESP_MAXIMUM_RETRY` (mặc định 5 lần) thì tiếp tục gọi `esp_wifi_connect()` và giữ đèn thở vàng; nếu quá 5 lần thì set cờ `WIFI_FAIL_BIT` và chuyển đèn sang `WIFI_STATUS_FAILED`.
    - Bắt `IP_EVENT_STA_GOT_IP` $\to$ Trích xuất địa chỉ IPv4, set cờ `WIFI_CONNECTED_BIT` và chuyển đèn sang `WIFI_STATUS_CONNECTED`.

### 2.2. Tại sao phải làm vậy? (Rationale)
1. **Tiết kiệm tài nguyên vi xử lý**: Quá trình bắt tay 4 bước WPA2 và xin cấp phát IP qua DHCP tốn từ 1 đến 5 giây. Cơ chế Event Group loại bỏ triệt để các vòng lặp chờ tích cực (polling/busy-wait).
2. **Chống treo hệ thống khi Router mất điện**: Cơ chế giới hạn số lần thử lại tối đa giúp ngăn chip gọi reconnect liên tục vô hạn, tránh hiện tượng nghẽn ngắt Wi-Fi và tràn bộ nhớ heap.

---

## 3. Quyết định: Cấu Hình Wi-Fi Linh Hoạt Qua Kconfig (`Kconfig.projbuild`)

### 3.1. Sửa, ghi thêm, cập nhật những gì?
- **Tạo mới [`main/Kconfig.projbuild`](file:///d:/Document/PBL5_stevejobvn/device_firmware/3_wifi_connection/main/Kconfig.projbuild)**:
  - Định nghĩa menu `Example Configuration` hỗ trợ cấu hình đồ họa qua `idf.py menuconfig`:
    - `CONFIG_ESP_WIFI_SSID`: Chuỗi ký tự tên Access Point.
    - `CONFIG_ESP_WIFI_PASSWORD`: Chuỗi ký tự mật khẩu Wi-Fi.
    - `CONFIG_ESP_MAXIMUM_RETRY`: Số lần thử lại kết nối tối đa (mặc định 5).
- **Tại [`main/app_main.c`](file:///d:/Document/PBL5_stevejobvn/device_firmware/3_wifi_connection/main/app_main.c)**:
  - Loại bỏ các chuỗi hardcode tĩnh `#define LIGHT_ESP_WIFI_SSID "YOUR-SSID"`.
  - Thay thế bằng macro đọc từ Kconfig, kèm khối bảo vệ `#ifndef` fallback tự động.

### 3.2. Tại sao phải làm vậy? (Rationale)
1. **Bảo mật và Vệ sinh Git (Git Hygiene)**: Tránh việc thành viên trong nhóm vô tình commit thông tin Wi-Fi và mật khẩu cá nhân lên remote GitHub công khai.
2. **Tiện ích phát triển nhanh**: Cho phép bất kỳ thành viên nào trong nhóm cũng có thể cấu hình kết nối mạng nhà mình chỉ qua giao diện menuconfig mà không phải sửa một dòng mã C nào.

---

## 4. Quyết định: Chuẩn Hóa Đa Mục Tiêu (ESP32-S3 & ESP32-C3) & Bảo Vệ Toolchain GCC 15

### 4.1. Sửa, ghi thêm, cập nhật những gì?
- **Tạo mới [`main/include/board_esp32s3_devkitc.h`](file:///d:/Document/PBL5_stevejobvn/device_firmware/3_wifi_connection/main/include/board_esp32s3_devkitc.h)**:
  - Cấu hình nút Boot trên ESP32-S3: `#define LIGHT_BUTTON_GPIO 0` (Active level 0).
  - Cấu hình chân DIN thanh LED WS2812B: `#define LIGHT_WS2818_GPIO 4` và `#define LIGHT_WS2818_NUM_LEDS 8`.
- **Cập nhật [`main/include/board_esp32c3_devkitc.h`](file:///d:/Document/PBL5_stevejobvn/device_firmware/3_wifi_connection/main/include/board_esp32c3_devkitc.h)**:
  - Loại bỏ các chân LED analog PWM cũ (`RED 3`, `GREEN 4`, `BLUE 5`, `COLD 7`, `WARM 10`).
  - Quy chuẩn chân DIN LED: `#define LIGHT_WS2818_GPIO 4` và `#define LIGHT_WS2818_NUM_LEDS 8`.
  - Giữ nguyên nút Boot trên ESP32-C3: `#define LIGHT_BUTTON_GPIO 9`.
- **Cập nhật [`main/CMakeLists.txt`](file:///d:/Document/PBL5_stevejobvn/device_firmware/3_wifi_connection/main/CMakeLists.txt)**:
  - Tự động nhận diện biến `IDF_TARGET` để nạp đúng header bo mạch tương ứng (`board_esp32s3_devkitc.h` hoặc `board_esp32c3_devkitc.h`).
  - Khai báo danh sách phụ thuộc rõ ràng: `PRIV_REQUIRES button app_storage light_driver esp_wifi esp_event esp_netif nvs_flash`.
- **Cập nhật [`CMakeLists.txt`](file:///d:/Document/PBL5_stevejobvn/device_firmware/3_wifi_connection/CMakeLists.txt)**:
  - Thêm `set(COMPONENTS main button app_storage light_driver)` ở đầu file (theo quy chuẩn ADR-001).

### 4.2. Tại sao phải làm vậy? (Rationale)
1. **Duy trì tính nhất quán song song S3 / C3**: Cho phép cả hai dòng chip của nhóm dùng chung một cấu trúc dự án thống nhất.
2. **Khắc phục lỗi Compiler Crash**: Lệnh `set(COMPONENTS ...)` loại bỏ các component dư thừa như `esp_lcd` khỏi cây build, ngăn chặn triệt để lỗi phân đoạn bộ nhớ (Segmentation Fault / ICE) của GCC 15.2.0 trên Windows.

---

## 5. Kết Quả Kiểm Thử Biên Dịch (Verification Matrix)

Cả hai mục tiêu phần cứng đều được biên dịch kiểm tra chéo thành công 100% với trình biên dịch ESP-IDF v6.0.2:

| Mục tiêu phần cứng | Kiến trúc CPU | Lệnh kiểm thử | Kết quả | Trạng thái nhị phân |
| :--- | :--- | :--- | :---: | :--- |
| **ESP32-S3** | Xtensa Dual-Core 240MHz | `idf.py -C device_firmware/3_wifi_connection build` | ✅ **Exit Code 0** | Tạo thành công `3_wifi_connection.bin` (`0xc7c70` bytes) |
| **ESP32-C3** | RISC-V Single-Core 160MHz | `idf.py -C device_firmware/3_wifi_connection build` | ✅ **Exit Code 0** | Tạo thành công `3_wifi_connection.bin` |

---

## 6. Hậu Quả & Đánh Giá (Consequences)

### Tích cực (Positive):
- **Phản hồi người dùng trực quan**: Thiết bị có khả năng tự báo hiệu trạng thái mạng bằng màu sắc đèn LED sinh động, nâng cao trải nghiệm sử dụng thực tế.
- **Tính mô-đun và độc lập cao**: Toàn bộ thay đổi gói gọn bên trong `device_firmware/3_wifi_connection/`, không làm ảnh hưởng đến tầng thư viện dùng chung `components` hay các chapter khác.
- **Tiện ích phát triển linh hoạt**: Thay đổi Wi-Fi nhanh chóng qua menuconfig, không làm bẩn lịch sử Git.
- **Đầy đủ tài liệu tiến độ**: Tích hợp báo cáo chi tiết tại `docs/progress/7.5 Practice - Wi-Fi Configuration in Smart Light Project.md`.

### Tiêu cực & Thách thức (Negative):
- **Chưa có cơ chế cấp phát mạng từ Smartphone**: Người dùng vẫn phải cấu hình SSID / Password trước khi nạp chip. Thách thức này sẽ được giải quyết triệt để ở **Mục 7.5.2 (Chương 4 `4_network_config`)** bằng cơ chế BLE Smart Provisioning qua App di động.
