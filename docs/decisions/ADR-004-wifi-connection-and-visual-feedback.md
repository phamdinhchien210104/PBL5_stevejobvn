# ADR-004: Kiến Trúc Kết Nối Wi-Fi Station, Cấp Phát Mạng Thông Minh Qua BLE và Phản Hồi Thị Giác Trên Thanh LED WS2812B (3_wifi_connection & 4_network_config)

- **Trạng thái**: Đã phê duyệt (Accepted)
- **Ngày quyết định**: 2026-09-19 (Cập nhật: 2026-09-21)
- **Phạm vi**: `device_firmware/3_wifi_connection`, `device_firmware/4_network_config`, `docs/progress/7.5 Practice - Wi-Fi Configuration in Smart Light Project.md`
- **Sơ đồ kiến trúc tương tác (Archify)**: [Xem sơ đồ trực quan (HTML)](./diagrams/ADR-004-wifi-connection-and-visual-feedback.html)

---

## Bối Cảnh & Vấn Đề (Context & Problem Statement)

Sau khi hoàn thiện tầng driver phần cứng cơ sở ở Chương 2 (**`2_light_drivers`**) theo [ADR-003](./ADR-003-light-drivers-and-button-modernization.md), dự án chuyển sang giai đoạn kết nối mạng không dây theo **Mục 7.5: Cấu Hình Wi-Fi Cho Dự Án Đèn Thông Minh (Wi-Fi Configuration in Smart Light Project)** trong giáo trình *ESP32-C3 Wireless Adventure*.

Mục 7.5 bao gồm hai giai đoạn tiến hóa kiến trúc:
1. **Giai đoạn 1 (7.5.1 Wi-Fi Connection - `3_wifi_connection`)**: Kết nối Wi-Fi Station cơ bản, yêu cầu cấu hình thông tin mạng cố định (hardcode hoặc qua Kconfig) và bắt tay 4 bước WPA2 với Router.
2. **Giai đoạn 2 (7.5.2 Smart Wi-Fi Configuration - `4_network_config`)**: Chuyển đổi đèn sang cơ chế cấp phát mạng thông minh không dây (Smart Provisioning) qua Bluetooth LE, cho phép người dùng cấu hình Wi-Fi trực tiếp từ smartphone qua mã QR Code trực quan mà không cần nạp lại mã nguồn.

Mã nguồn gốc của giáo trình gặp phải các xung đột kiến trúc và vấn đề thực nghiệm nghiêm trọng:
1. **Lạc hậu tầng Driver LED**: Cả hai dự án mẫu đều gọi 5 kênh LED PWM analog cũ (`gpio_red`, `gpio_green`, `gpio_blue`, `gpio_cold`, `gpio_warm`), không tương thích với **Thanh LED WS2812B 8-Bit NeoPixel** đã chuẩn hóa trong ADR-003.
2. **Thiếu phản hồi trạng thái mạng cho người dùng**: Khi chip dò tìm Wi-Fi, bắt tay 4 bước WPA2, hoặc mất kết nối, hệ thống chỉ in log UART. Trên thiết bị đèn thực tế không có màn hình hiển thị, người dùng không thể biết trạng thái mạng hiện tại.
3. **Xung đột máy trạng thái Wi-Fi trong Provisioning**: Việc đăng ký sớm `WIFI_EVENT` trong quá trình cấp phát BLE khiến event handler người dùng tranh chấp quyền điều khiển với `network_provisioning`, dẫn đến lỗi treo / xoay vô hạn trên ứng dụng điện thoại.
4. **Nhầm lẫn giữa các ứng dụng di động trong hệ sinh thái Espressif**: Sự khác biệt giữa cấp phát Wi-Fi cục bộ (ESP BLE Provisioning) và cấp phát kèm xác thực đám mây (ESP RainMaker) chưa được phân định rõ ràng.

ADR này ghi nhận toàn bộ các quyết định kiến trúc cho cả hai dự án `3_wifi_connection` và `4_network_config`.

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
- **Tạo mới [`main/include/board_esp32s3_devkitc.h`](file:///d:/Document/PBL5_stevejobvn/device_firmware/3_wifi_connection/main/include/board_esp32s3_devkitc.h)** (cho cả `3_wifi_connection` và `4_network_config`):
  - Cấu hình nút Boot trên ESP32-S3: `#define LIGHT_BUTTON_GPIO 0` (Active level 0).
  - Cấu hình chân DIN thanh LED WS2812B: `#define LIGHT_WS2818_GPIO 4` và `#define LIGHT_WS2818_NUM_LEDS 8`.
- **Cập nhật [`main/include/board_esp32c3_devkitc.h`](file:///d:/Document/PBL5_stevejobvn/device_firmware/3_wifi_connection/main/include/board_esp32c3_devkitc.h)**:
  - Loại bỏ các chân LED analog PWM cũ (`RED 3`, `GREEN 4`, `BLUE 5`, `COLD 7`, `WARM 10`).
  - Quy chuẩn chân DIN LED: `#define LIGHT_WS2818_GPIO 4` và `#define LIGHT_WS2818_NUM_LEDS 8`.
  - Giữ nguyên nút Boot trên ESP32-C3: `#define LIGHT_BUTTON_GPIO 9`.
- **Cập nhật [`main/CMakeLists.txt`](file:///d:/Document/PBL5_stevejobvn/device_firmware/3_wifi_connection/main/CMakeLists.txt)**:
  - Tự động nhận diện biến `IDF_TARGET` để nạp đúng header bo mạch tương ứng (`board_esp32s3_devkitc.h` hoặc `board_esp32c3_devkitc.h`).
  - Khai báo danh sách phụ thuộc rõ ràng: `PRIV_REQUIRES button app_storage light_driver esp_wifi esp_event esp_netif nvs_flash bt`.
- **Cập nhật [`CMakeLists.txt`](file:///d:/Document/PBL5_stevejobvn/device_firmware/3_wifi_connection/CMakeLists.txt)**:
  - Thêm `set(COMPONENTS main button app_storage light_driver)` ở đầu file (theo quy chuẩn ADR-001).

### 4.2. Tại sao phải làm vậy? (Rationale)
1. **Duy trì tính nhất quán song song S3 / C3**: Cho phép cả hai dòng chip của nhóm dùng chung một cấu trúc dự án thống nhất.
2. **Khắc phục lỗi Compiler Crash**: Lệnh `set(COMPONENTS ...)` loại bỏ các component dư thừa như `esp_lcd` khỏi cây build, ngăn chặn triệt để lỗi phân đoạn bộ nhớ (Segmentation Fault / ICE) của GCC 15.2.0 trên Windows.

---

## 5. Quyết định: Cấp Phát Mạng Thông Minh Qua BLE, Tách Biệt Máy Trạng Thái Wi-Fi & Xử Lý Sự Cố Kết Nối (4_network_config)

### 5.1. Sửa, ghi thêm, cập nhật những gì?
- **Quản lý phụ thuộc hiện đại qua ESP Component Manager**:
  - Tạo `main/idf_component.yml` tự động tải `espressif/network_provisioning` và `espressif/qrcode`.
  - Bổ sung khối macro tương thích ngược `#if __has_include("network_provisioning/manager.h")`.
- **Phân tách hoàn toàn `WIFI_EVENT` khỏi giai đoạn Cấp phát**:
  - Trong `wifi_initialize()`, không đăng ký `WIFI_EVENT` để tránh việc handler người dùng gọi `esp_wifi_connect()` sớm khi credentials chưa có, gây cạn kiệt số lần retry và nghẽn máy trạng thái nội bộ của `network_provisioning`.
  - Chỉ đăng ký `WIFI_EVENT` sau khi hoàn tất cấp phát (`WIFI_PROV_END`) hoặc khi thiết bị đã được cấu hình trước đó (`wifi_prov_mgr_is_provisioned == true`).
- **Cấu hình số lần thử lại & Reset máy trạng thái khi thất bại**:
  - Cấu hình `.network_prov_wifi_conn_cfg = { .wifi_conn_attempts = 5 }`.
  - Khi gặp sự kiện `WIFI_PROV_CRED_FAIL`, tự động gọi `wifi_prov_mgr_reset_sm_state_on_failure()` giúp app trên điện thoại nhận mã lỗi và cho phép người dùng nhập lại ngay lập tức mà không bị treo/xoay vô hạn.
- **Tự động giải phóng tài nguyên BLE**:
  - Khi nhận `WIFI_PROV_END` và có IP, hệ thống gọi `wifi_prov_mgr_deinit()` thu hồi toàn bộ vùng nhớ BLE stack (NimBLE), giải phóng hàng chục KB heap cho tác vụ đèn.
- **Hệ thống phản hồi 4 trạng thái trên LED WS2812B**:
  - `PROV_STATUS_WAITING`: Thở Xanh dương (Cyan) báo hiệu đang phát BLE chờ kết nối.
  - `PROV_STATUS_CONNECTING`: Thở Vàng khi đang nhận thông tin và thử kết nối Router.
  - `PROV_STATUS_SUCCESS`: Sáng tĩnh Xanh lá cây khi nhận IP thành công.
  - `PROV_STATUS_FAILED`: Báo Đỏ cảnh báo khi nhập sai mật khẩu Wi-Fi.

### 5.2. Tại sao phải làm vậy? (Rationale)
1. **Khắc phục lỗi ứng dụng điện thoại bị treo / xoay vô hạn**: Đảm bảo chu trình cấp phát mạng diễn ra tuần tự, tin cậy và có khả năng phục hồi khi người dùng nhập sai mật khẩu.
2. **Tối ưu hóa tài nguyên phần cứng**: Thu hồi hoàn toàn bộ nhớ vô tuyến Bluetooth sau khi đã hoàn thành nhiệm vụ cấp phát.

---

## 6. Quyết định: Lựa Chọn Ứng Dụng Di Động & Phân Tách Ranh Giới Giữa Local Provisioning và Cloud Claiming

### 6.1. Bối cảnh & Vấn đề thực nghiệm
Trong quá trình thực nghiệm Mục 7.5.2 trên smartphone, người dùng có nhiều ứng dụng do Espressif phát hành: **ESP BLE Provisioning**, **ESP RainMaker**, và **ESP RainMaker Classic**.

Khi kiểm thử thực tế với firmware `device_firmware/4_network_config`:
- Sử dụng ứng dụng **ESP BLE Provisioning**: Quét mã QR $\to$ Nhận diện thiết bị `PROV_XXXXXX` $\to$ Nhập PoP `abcd1234` $\to$ Cấp phát Wi-Fi thành công 100%, đèn sáng Xanh lá cây.
- Sử dụng ứng dụng **ESP RainMaker Classic**: Quét vẫn tìm thấy thiết bị BLE, nhưng **không thể kết nối Wi-Fi thành công** (bị dừng lại ở bước hoàn tất).

### 6.2. Phân tích nguyên nhân kỹ thuật kiến trúc
Sự khác biệt cốt lõi nằm ở giao thức tầng ứng dụng (Application Layer Protocol) trên bảng thuộc tính BLE GATT:

| Tiêu Chí | Ứng Dụng **ESP BLE Provisioning** (`esp_prov`) | Ứng Dụng **ESP RainMaker / RainMaker Classic** (`esp-rainmaker`) |
| :--- | :--- | :--- |
| **Mục đích thiết kế** | **Cấp phát Wi-Fi cục bộ thuần túy** (Local Wi-Fi Onboarding). | **Cấp phát Wi-Fi + Xác thực Đám mây (Cloud Claiming) & Gán Thiết bị vào Tài khoản (User-Node Association)**. |
| **GATT Endpoints yêu cầu** | Chỉ yêu cầu 4 endpoint tiêu chuẩn: `prov-session`, `prov-config`, `proto-ver`, `prov-scan`. | Ngoài 4 endpoint Wi-Fi, **bắt buộc phải có thêm**:<br>• **`cloud_user_assoc`**: Gửi ID người dùng để liên kết thiết bị với tài khoản RainMaker Cloud.<br>• **`rmaker_claim`**: Thực hiện bắt tay Assisted Claiming. |
| **Yêu cầu phân vùng Flash** | Chỉ cần phân vùng `nvs` thông thường. | Bắt buộc có phân vùng **`fctry`** (chứa chứng chỉ TLS X.509 để kết nối AWS IoT) và phân vùng **`sec_cert`**. |
| **Hành vi trên Chương 4 (`4_network_config`)** | ✅ **Thành công 100%**: Firmware đáp ứng đúng các endpoint Wi-Fi mà app yêu cầu. | ❌ **Thất bại**: App chờ phản hồi từ endpoint `cloud_user_assoc`, nhưng firmware Chương 4 là firmware Wi-Fi cục bộ, không chạy RainMaker Agent nên không mở endpoint này. |

### 6.3. Quyết định kiến trúc & Phân chia lộ trình cho các Chương
1. **Đối với Chương 4 (`4_network_config`)**:
   - **Bắt buộc chuẩn hóa quy trình sử dụng duy nhất ứng dụng ESP BLE Provisioning**.
   - Không nạp dư thừa RainMaker Agent vào Chương 4 để giữ nguyên tính độc lập của bài học cấu hình mạng cục bộ theo đúng giáo trình.
2. **Đối với Chương 5 (`5_rainmaker`), Chương 6 (`6_project_optimize`) và Chương 7 (`7_insights`)**:
   - Firmware sẽ tích hợp thư viện `esp_rainmaker_core` và component `app_wifi`.
   - Firmware tự động mở endpoint `cloud_user_assoc` và sử dụng cấu trúc `partitions.csv` có phân vùng `fctry` (`0x340000`).
   - **Lúc này, ứng dụng ESP RainMaker / RainMaker Classic sẽ là ứng dụng chính thức**: Vừa cấp phát Wi-Fi, vừa tự động gán thiết bị vào tài khoản điện thoại và tự động vẽ giao diện điều khiển đèn thông minh đa màu sắc.

---

## 7. Kết Quả Kiểm Thử Biên Dịch & Thực Nghiệm Phần Cứng (Verification Matrix)

| Dự án Firmware | Mục tiêu phần cứng | Lệnh kiểm thử | Kết quả Biên dịch | Kiểm thử Mạch thật & Ứng dụng |
| :--- | :--- | :--- | :---: | :--- |
| **3_wifi_connection** | ESP32-S3 | `idf.py -C device_firmware/3_wifi_connection build` | ✅ **Exit Code 0** | Tạo thành công `3_wifi_connection.bin` |
| **3_wifi_connection** | ESP32-C3 | `idf.py -C device_firmware/3_wifi_connection build` | ✅ **Exit Code 0** | Kết nối router thành công, LED sáng Xanh lá, nhận IP |
| **4_network_config** | ESP32-S3 | `idf.py -C device_firmware/4_network_config build` | ✅ **Exit Code 0** | Tạo thành công `4_network_config.bin` |
| **4_network_config** | ESP32-C3 | `idf.py -C device_firmware/4_network_config build flash monitor` | ✅ **Exit Code 0** | **Quét QR qua ESP BLE Provisioning thành công 100%**, cấp Wi-Fi mượt mà, LED chuyển từ Thở Xanh dương $\to$ Thở Vàng $\to$ Sáng Xanh lá |

---

## 8. Hậu Quả & Đánh Giá (Consequences)

### Tích cực (Positive):
- **Hoàn tất trọn vẹn Mục 7.5**: Đáp ứng 100% yêu cầu kỹ thuật của cả 7.5.1 (Wi-Fi STA) và 7.5.2 (BLE Smart Provisioning).
- **Trải nghiệm người dùng thông minh**: Cấp phát Wi-Fi qua Bluetooth LE bằng mã QR Code tiện lợi, phản hồi màu sắc LED trực quan, tự động giải phóng RAM khi có IP.
- **Tính ổn định cao**: Khắc phục triệt để lỗi treo / xoay vô hạn bằng cơ chế phân tách `WIFI_EVENT` và reset state machine.
- **Ranh giới kiến trúc rõ ràng**: Định hình chính xác việc sử dụng app **ESP BLE Provisioning** cho cấu hình mạng cục bộ (Chương 4) và chuẩn bị sẵn sàng cho **ESP RainMaker** ở các chương kết nối Cloud (Chương 5, 6, 7).
