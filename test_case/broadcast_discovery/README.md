# Test Case 1: Khám Phá Thiết Bị Qua UDP Broadcast (`test_case/broadcast_discovery`)

> **Căn cứ giáo trình**: Mục 8.2.1 *Broadcast* — Sách *ESP32-C3 Wireless Adventure: A Comprehensive Guide to IoT* (trang 156–161).  
> **Dự án**: PBL5 Smart Light Architecture (Hỗ trợ đa mục tiêu ESP32-C3 & ESP32-S3 trên ESP-IDF v6.0.2).

---

## 1. Đối Chiếu Cái Cũ vs Cái Mới (What was Old vs What is New)

| Thành phần | Mã nguồn gốc (Cũ) | Kiến trúc hiện đại hóa (Mới) |
|---|---|---|
| **Hệ thống Build** | Đường dẫn component cũ `../../Project/components/...` (lỗi build) | Kế thừa chuẩn `device_firmware/components`, bảo vệ chống crash **GCC 15 ICE** |
| **Phần cứng LED** | 5 kênh PWM LED rời rạc (LEDC) | Thanh 8 LED **WS2812B NeoPixel** điều khiển qua **Hardware SPI2 DMA @ 3.2MHz** trên GPIO 4 |
| **Nút bấm** | Gán cứng GPIO 9, chỉ có 1 callback cơ bản | **IoT Button HAL** đa cử chỉ: Nhấn 1 lần (Bật/Tắt), Đúp (Đổi 8 màu), Nhấn giữ (Dimming vô cấp) |
| **Hỗ trợ Chip** | Chỉ hỗ trợ duy nhất ESP32-C3 | **Đa mục tiêu**: Tự động nhận diện **ESP32-C3** (Boot GPIO 9) và **ESP32-S3** (Boot GPIO 0) |
| **Cơ chế mạng Wi-Fi** | Ghim cứng WPA2, không có mã vùng VN, dễ rớt mạng | Ngăn xếp Wi-Fi cải tiến: WPA2/WPA3 Personal, PMF, dải kênh VN 1-13, HT20, công suất 12dBm, auto-retry |
| **Chế độ chạy** | Ghim cứng cờ `LIGHT_BROADCAST_CLIENT 1` chạy 1 lần rồi đứng | Chạy server UDP đa nhiệm nền FreeRTOS (`broadcast_server_task`), cấu hình được qua `Kconfig` |
| **Công cụ kiểm thử** | Phụ thuộc lệnh Linux `socat` / Netcat phức tạp | Bổ sung script Python tự động [`scripts/test_broadcast_discovery.py`](scripts/test_broadcast_discovery.py) |

---

## 2. Các Điểm Chỉnh Sửa Cốt Lõi (Core Modernization Points)

1. **Khắc phục lỗi biên dịch CMake & GCC 15**:
   - Khai báo danh sách `set(COMPONENTS main button app_storage light_driver nvs_flash esp_wifi esp_event esp_netif)`.
   - Trỏ `EXTRA_COMPONENT_DIRS` chính xác vào thư mục `device_firmware/components`.
2. **Cấu trúc lại luồng xử lý Socket UDP Broadcast**:
   - Server lắng nghe trên cổng `3333` bằng socket `SOCK_DGRAM`.
   - Khi nhận bản tin `"Are you Espressif IOT Smart Light"`, server tự động trích xuất IP/Port của sender và gửi trả gói Unicast `"ESP32-C3 Smart Light https 443"`.
   - Kích hoạt phản hồi thị giác: Nhấp nháy thanh LED WS2812B xanh lá cây khi nhận diện thành công.

---

## 3. Hướng Dẫn Biên Dịch, Nạp & Kiểm Thử

### Bước 1: Biên dịch và nạp firmware lên ESP32-C3
```powershell
idf.py -C test_case/broadcast_discovery -p COM3 flash monitor
```

### Bước 2: Quan sát log trên ESP32 Serial Monitor
ESP32 sẽ kết nối Wi-Fi và hiển thị:
```text
I (...) broadcast_discovery: ==> [Wi-Fi] KẾT NỐI THÀNH CÔNG! ĐÃ CÓ ĐỊA CHỈ IP: 192.168.1.x
I (...) broadcast_discovery: UDP Broadcast Server đang lắng nghe trên cổng 3333...
```

### Bước 3: Chạy script kiểm thử từ máy tính (cùng mạng Wi-Fi)
Mở một cửa sổ PowerShell mới trên máy tính:
```powershell
python scripts/test_broadcast_discovery.py
```

**Kết quả mong đợi trên máy tính**:
```text
[1] Đang gửi gói tin UDP Broadcast tới 255.255.255.255:3333...
[2] Đang lắng nghe phản hồi Unicast từ ESP32...
==> [THÀNH CÔNG] Nhận phản hồi từ thiết bị 192.168.1.x:3333:
    Dữ liệu dịch vụ: 'ESP32-C3 Smart Light https 443'
```
Đồng thời trên terminal ESP32 sẽ in ra:
```text
Receive udp broadcast from 192.168.1.15:52341, data is Are you Espressif IOT Smart Light
Message sent successfully -> Phản hồi unicast tới 192.168.1.15:52341: 'ESP32-C3 Smart Light https 443'
```
