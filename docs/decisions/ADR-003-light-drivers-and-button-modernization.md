# ADR-003: Kiến Trúc Tầng Điều Khiển Thiết Bị (2_light_drivers), Hiện Đại Hóa Button ADC và Chuẩn Hóa Phần Cứng WS2812B NeoPixel

- **Trạng thái**: Đã phê duyệt (Accepted)
- **Ngày quyết định**: 2026-09-19
- **Phạm vi**: `device_firmware/2_light_drivers`, `device_firmware/components/{button, light_driver, app_storage}`, `.vscode/`, `.gitignore`
- **Sơ đồ kiến trúc tương tác (Archify)**: [Xem sơ đồ trực quan (HTML)](./diagrams/ADR-003-light-drivers-and-button-modernization.html)

---

## Bối Cảnh & Vấn Đề (Context & Problem Statement)

Chương 2 (**`device_firmware/2_light_drivers`**) kế thừa từ giáo trình *ESP32-C3 Wireless Adventure*, ban đầu được thiết kế cho:
1. Chip vi điều khiển duy nhất: **ESP32-C3** chạy trên nền **ESP-IDF v4.3.2**.
2. Phần cứng phát quang: Mạch đèn LED analog 5 kênh riêng biệt (Red, Green, Blue, Cold White, Warm White) sử dụng xung PWM phần cứng (**LEDC**) kết hợp bộ định thời phần cứng legacy (`driver/timer.h`).
3. Nút bấm phân áp: Dùng driver `driver/adc.h` cũ đã bị Espressif loại bỏ hoàn toàn trong ESP-IDF v5 và v6.

Khi chuyển giao sang dự án thực tế PBL5 với phiên bản **ESP-IDF v6.0.2**, hệ thống gặp phải các xung đột nghiêm trọng:
- **Xung đột phần cứng**: Phần cứng thực tế của nhóm sử dụng **Thanh LED WS2812B 8-Bit NeoPixel (`CJMCU-2812-8`)** giao tiếp số đơn tuyến (Single-wire NRZ), không thể dùng trực tiếp 5 kênh PWM analog như sách. Đồng thời, nhóm có cả thành viên dùng **ESP32-S3-DevKitC-1-N16R8** và **ESP32-C3 DevKit**.
- **Lỗi biên dịch Breaking Changes**: Thư viện `driver/adc.h` và `driver/timer.h` không còn tồn tại trên ESP-IDF v6, dẫn tới lỗi biên dịch trên `button_adc.c` và `iot_led.c`.
- **Xung đột môi trường nhóm (Multi-Developer Git Pollution)**: Việc commit các file cấu hình IDE chứa đường dẫn tuyệt đối cá nhân (`C:\esp\...`, `COM8`) và file binary cache `.cache/clangd/*.idx` làm hỏng môi trường phát triển của các thành viên khác.

ADR này ghi nhận toàn bộ các quyết định tái cấu trúc kiến trúc mã nguồn để giải quyết dứt điểm các vấn đề trên.

---

## 1. Quyết định: Ánh xạ Tầng Light Driver sang Hardware SPI2 DMA điều khiển Thanh LED WS2812B 8-Bit NeoPixel

### 1.1. Sửa, ghi thêm, cập nhật những gì?
- **Module mới `ws2812_driver`**:
  - Tạo mới [`ws2812_driver.h`](file:///d:/Document/PBL5_stevejobvn/device_firmware/components/light_driver/include/ws2812_driver.h) và [`ws2812_driver.c`](file:///d:/Document/PBL5_stevejobvn/device_firmware/components/light_driver/ws2812_driver.c).
  - Sử dụng ngoại vi **Hardware SPI2 (`SPI2_HOST`)** với xung nhịp **3.2 MHz** và kênh truyền **DMA**.
  - Áp dụng bảng tra cứu (LUT) mã hóa: mỗi 2 bit dữ liệu WS2812 được biểu diễn bằng 1 byte SPI ($1\text{ bit SPI} = 312.5\text{ ns}$, 4 bit SPI = $1.25\,\mu\text{s}$ cho 1 bit xung WS2812). Xung Reset được duy trì $> 300\,\mu\text{s}$ bằng đệm số 0 cuối frame DMA.
- **Tái cấu trúc [`light_driver.c`](file:///d:/Document/PBL5_stevejobvn/device_firmware/components/light_driver/light_driver.c)**:
  - Bỏ phụ thuộc vào `iot_led.c` (driver PWM cũ phụ thuộc `driver/timer.h` đã lỗi thời).
  - Tích hợp FreeRTOS Software Timer (`s_effect_timer`, chu kỳ 30 ms) để hiện thực hóa hiệu ứng Thở (Breathing) theo sóng Cosine sinh học và hiệu ứng Nhấp nháy (Blink).
- **Mở rộng cấu trúc cấu hình [`light_driver.h`](file:///d:/Document/PBL5_stevejobvn/device_firmware/components/light_driver/include/light_driver.h)**:
  - Bổ sung trường `gpio_num_t gpio_ws2812;` và `uint16_t num_leds;` vào struct `light_driver_config_t`.
  - Trong `light_driver_init()`, gọi trực tiếp `ws2812_init(config->gpio_ws2812, config->num_leds)`.
- **Quy chuẩn chân kết nối**:
  - Tại [`board_esp32s3_devkitc.h`](file:///d:/Document/PBL5_stevejobvn/device_firmware/2_light_drivers/main/include/board_esp32s3_devkitc.h): Khai báo `#define LIGHT_WS2818_GPIO 4` và `#define LIGHT_WS2818_NUM_LEDS 8`.
  - Tại [`board_esp32c3_devkitc.h`](file:///d:/Document/PBL5_stevejobvn/device_firmware/2_light_drivers/main/include/board_esp32c3_devkitc.h): Khai báo `#define LIGHT_WS2818_GPIO 4` và `#define LIGHT_WS2818_NUM_LEDS 8`.
  - Cập nhật [`app_driver.c`](file:///d:/Document/PBL5_stevejobvn/device_firmware/2_light_drivers/main/app_driver.c) truyền tường minh 2 thông số trên vào `driver_config`.

### 1.2. Tại sao phải thay đổi? (Rationale)
1. **Tương thích phần cứng thực tế**: Nhóm sử dụng module LED dây số WS2812B 8 bóng. Việc ép phần cứng chạy theo mạch 5 bóng analog cũ trong sách là bất khả thi và lãng phí tài nguyên chân.
2. **Khắc phục lỗi định thời bằng Hardware SPI DMA**: Giao thức NZR 800 kHz của WS2812B yêu cầu độ chính xác thời gian ở mức nano-giây. Nếu dùng GPIO Bit-banging thông thường, các ngắt Wi-Fi/Bluetooth của FreeRTOS sẽ làm biến dạng xung, gây nháy sai màu. Dùng Hardware SPI DMA giải phóng hoàn toàn CPU và bảo đảm tín hiệu phát ra ổn định 100%.
3. **Bảo tồn toàn vẹn Interface cấp cao**: Toàn bộ chữ ký hàm công khai của thư viện (`light_driver_set_switch`, `set_rgb`, `set_hsv`, `set_ctb`, `breath_start`, `blink_start`) được giữ nguyên vẹn 100%, giúp các chương tiếp theo (Wi-Fi, Provisioning, RainMaker) tái sử dụng mà không cần thay đổi code.

### 1.3. Giải pháp thay thế đã xem xét (Alternatives Considered)
- **Dùng RMT Driver (`esp_driver_rmt`)**: RMT là ngoại vi chuyên dụng của Espressif cho addressable LED. Tuy nhiên, API RMT đã bị đập đi xây lại hoàn toàn giữa IDF v4 (`driver/rmt.h`) và v5/v6 (`driver/rmt_tx.h` với encoder callback). Giải pháp SPI DMA 3.2MHz nhỏ gọn hơn, độc lập nền tảng, dễ cấu hình và chạy mượt mà trên cả S3 và C3 mà không cần kéo theo toàn bộ RMT driver phụ thuộc.

---

## 2. Quyết định: Nâng cấp Toàn diện Module `components/button` lên `esp_adc/adc_oneshot.h`

### 2.1. Sửa, ghi thêm, cập nhật những gì?
- **Xóa bỏ stub tạm bợ**: Xóa tệp `button_adc_stub.c` vốn chỉ trả về `ESP_ERR_NOT_SUPPORTED`.
- **Tích hợp bản chuẩn từ nhánh Chương 3 (`feat/wifi-connection`)**:
  - Cập nhật [`button_adc.c`](file:///d:/Document/PBL5_stevejobvn/device_firmware/components/button/button_adc.c) và [`include/button_adc.h`](file:///d:/Document/PBL5_stevejobvn/device_firmware/components/button/include/button_adc.h) sang bộ driver chuẩn mới:
    - Sử dụng `adc_oneshot_unit_handle_t`, `adc_oneshot_new_unit`, `adc_oneshot_read`, `adc_oneshot_del_unit`.
    - Tích hợp cơ chế cân chỉnh điện áp `esp_adc/adc_cali.h` (`adc_cali_handle_t`, `adc_cali_raw_to_voltage`).
    - Lấy mẫu đa điểm `NO_OF_SAMPLES = 64` để lọc nhiễu và chống trôi điện áp.
- **Sửa phụ thuộc CMake [`components/button/CMakeLists.txt`](file:///d:/Document/PBL5_stevejobvn/device_firmware/components/button/CMakeLists.txt)**:
  - Đổi từ `PRIV_REQUIRES` sang `REQUIRES esp_adc esp_driver_gpio esp_timer` để xuất public header `esp_adc/adc_oneshot.h` cho các component khác khi gọi `iot_button.h`.

### 2.2. Tại sao phải thay đổi? (Rationale)
- File `button_adc_stub.c` tạo ra ở nhánh Chapter 2 chỉ là giải pháp "chữa cháy" tạm thời để bypass compiler, làm mất hoàn toàn chức năng đọc bàn phím phân áp ADC.
- Việc nhánh Chapter 3 đã nâng cấp chuẩn `adc_oneshot` nhưng Chapter 2 lại dùng stub sẽ gây ra xung đột nghiêm trọng (Merge Conflict) khi hợp nhất vào `main`.
- Đưa bản chuẩn `adc_oneshot` vào Chapter 2 ngay lúc này mang lại 2 lợi ích: vừa hỗ trợ đầy đủ phím GPIO lẫn phím ADC, vừa bảo đảm Git tự động hòa giải 0 xung đột khi merge Chapter 3.

---

## 3. Quyết định: Mở rộng Trải nghiệm Tương tác Nút Bấm Vật Lý (Gesture Mapping)

### 3.1. Sửa, ghi thêm, cập nhật những gì?
Tại [`app_driver.c`](file:///d:/Document/PBL5_stevejobvn/device_firmware/2_light_drivers/main/app_driver.c), ngoài sự kiện nhả nút bấm theo giáo trình, đăng ký thêm các gesture tương tác:
- **`BUTTON_PRESS_UP` / `BUTTON_SINGLE_CLICK`**: Đảo trạng thái Bật $\leftrightarrow$ Tắt đèn (`light_driver_set_switch`).
- **`BUTTON_DOUBLE_CLICK`**: Duyệt xoay vòng qua danh sách 8 màu sắc mẫu (Trắng ấm, Đỏ, Xanh lá, Xanh dương, Vàng, Tím, Cyan, Trắng lạnh) và tự động ghi đè Flash NVS (`light_driver_set_rgb`).
- **`BUTTON_LONG_PRESS_START`**: Bật hoặc Tắt hiệu ứng Thở Cosine (`light_driver_breath_start` / `stop`).

### 3.2. Tại sao phải thay đổi? (Rationale)
- Sách giáo trình chỉ hướng dẫn bắt sự kiện nhả nút để bật/tắt đèn. Tuy nhiên, các tính năng cốt lõi khác như đổi màu RGB, đổi nhiệt độ màu CTB, hiệu ứng thở Breathing đều chưa có giao diện điều khiển nếu chưa học tới chương Wi-Fi/Cloud.
- Việc ánh xạ Double Click và Long Press cho phép kỹ sư kiểm thử 100% các API của tầng driver ngay trên kit phần cứng độc lập.

---

## 4. Quyết định: Chuẩn Hóa Cấu Hình IDE Đa Thành Viên (Multi-Developer Hygiene)

### 4.1. Sửa, ghi thêm, cập nhật những gì?
- **Khóa theo dõi file cá nhân**:
  - Chạy `git rm --cached` cho `.vscode/settings.json`, `device_firmware/1_blink/.vscode/settings.json`, `device_firmware/2_light_drivers/.vscode/settings.json`.
  - Cập nhật [`.gitignore`](file:///d:/Document/PBL5_stevejobvn/.gitignore) bổ sung:
    ```gitignore
    .vscode/settings.json
    **/.vscode/settings.json
    **/.cache/
    *.idx
    ```
- **Xóa bỏ rác binary cache**:
  - Chạy `git rm -r --cached` xóa bỏ 6 tệp binary index `.cache/clangd/index/*.idx` bị commit nhầm.
- **Tạo tệp mẫu chung**:
  - Tạo mới [`.vscode/settings.json.template`](file:///d:/Document/PBL5_stevejobvn/.vscode/settings.json.template) chứa các thiết lập chuẩn không chứa đường dẫn tuyệt đối.
- **Tài liệu hóa quy trình**:
  - Bổ sung mục `3. Bước 3: Cấu hình Extension trên Máy Cá nhân` và `5.4. Quy tắc Giữ sạch Git Repository` vào [README.md](file:///d:/Document/PBL5_stevejobvn/README.md).

### 4.2. Tại sao phải thay đổi? (Rationale)
- Trong môi trường làm việc nhóm, mỗi thành viên có đường dẫn cài đặt khác nhau (`C:\esp\...` vs `D:\esp\...`), cổng nạp khác nhau (`COM3`, `COM4`, `COM8`).
- Việc commit file `settings.json` cấp Workspace sẽ phá vỡ môi trường của các thành viên khác khi họ `git pull`. Việc chuyển cấu hình máy cá nhân sang **User Settings** (`%APPDATA%\Code\User\settings.json`) triệt tiêu 100% rủi ro này.

---

## Hệ Quả Kỹ Thuật (Consequences)

### Tích cực:
1. **Tính tương thích chéo**: Firmware biên dịch thành công 100% trên cả **ESP32-S3** và **ESP32-C3** với mã thoát 0 trên ESP-IDF v6.0.2.
2. **Khả năng mở rộng**: Tầng driver nút bấm sẵn sàng cho cả nút nhấn trên bo mạch, nút cơ rời ngoài và cụm phím ADC phân áp.
3. **Phần cứng ổn định**: Tín hiệu điều khiển thanh LED WS2812B 8 hạt qua Hardware SPI DMA chuẩn xác tuyệt đối, không phụ thuộc vào trạng thái ngắt của CPU.
4. **Git sạch sẽ**: Loại bỏ hoàn toàn các file rác index nhị phân, không còn xung đột file cấu hình cá nhân giữa các thành viên.
5. **Zero Merge Conflict**: Đồng bộ hoàn toàn `components/button` với nhánh Chương 3 (`feat/wifi-connection`).

### Tiêu cực & Biện pháp khắc phục:
- **Chiếm dụng bộ điều khiển SPI2**: Vì chân DIN của LED WS2812B sử dụng SPI2 MOSI, nếu sau này dự án cần gắn thêm màn hình SPI ngoài thì màn hình sẽ cần chia sẻ bus SPI hoặc sử dụng chân SPI khác. (Đã kiểm chứng: Dự án PBL5 hiện không yêu cầu màn hình SPI, chân GPIO 4 đáp ứng hoàn hảo).
