# Test Case 3: mDNS Discovery (`test_case/mdns_discovery`)

> **Căn cứ tài liệu**: Mục 8.2.4 *Multicast Application Protocol mDNS for Local Discovery* — Sách *ESP32-C3 Wireless Adventure: A Comprehensive Guide to IoT* (trang 169–172).  
> **Nền tảng**: ESP-IDF v6.0.2 | GCC 15 | FreeRTOS | mDNS Responder (RFC 6762 / Apple Bonjour).

---

## 1. So Sánh Kiến Trúc: Mã Nguồn Cũ vs Mã Nguồn Mới Cải Tiến

| Thành phần / Tiêu chí | Mã nguồn gốc trong sách (Cũ) | Mã nguồn cải tiến chuẩn PBL5 (Mới) |
| :--- | :--- | :--- |
| **Hệ thống LED chỉ thị** | PWM LED 5 kênh (RGBWC) rải rác. | **WS2812B Addressable LED Strip (8 hạt)** điều khiển bằng **Hardware SPI2 DMA @ 3.2MHz**. |
| **Phản hồi trực quan** | Chỉ log qua cổng Serial UART. | **Hiệu ứng nhấp nháy LED xanh lá (Green Pulse)** tức thì khi có request HTTP qua hostname mDNS. |
| **Nút bấm vật lý HAL** | Đọc GPIO cơ bản không có debounce/gestures. | **IoT Button Driver đa cử chỉ**: Nhấn 1 lần (Bật/Tắt), Nhấn đúp (Đổi 5 màu), Nhấn giữ (Chỉnh độ sáng). |
| **Kiến trúc phần cứng** | Gắn cứng riêng cho ESP32-C3 DevKit. | **Dual-Target linh hoạt**: Tự động nhận diện ESP32-C3 (Boot GPIO 9) và ESP32-S3 (Boot GPIO 0). |
| **Dịch vụ mạng tương tác** | Chỉ quảng bá mDNS rỗng, không có server HTTP thực tế để kiểm tra. | Tích hợp **HTTP Server nhẹ trên cổng 80** để test truy cập trực tiếp bằng trình duyệt qua `http://my_smart_light.local`. |
| **Tương thích ESP-IDF v6.0** | Dùng component mDNS cũ trong core ESP-IDF v4 (đã bị loại bỏ trong v5/v6). | Tích hợp component **`espressif/mdns` v1.4.0** chuẩn hóa qua IDF Component Manager và bảo vệ chống lỗi GCC 15 ICE. |
| **Quản lý Wi-Fi** | Wi-Fi Station cơ bản, không có PMF hay cấu hình bảo mật hiện đại. | Cấu hình **WIFI_AUTH_OPEN threshold**, **PMF**, Mã vùng **VN**, băng thông 20MHz ổn định. |

---

## 2. Các Điểm Chỉnh Sửa Cốt Lõi Trên ESP-IDF v6.0.2

1. **Tích hợp Component `espressif__mdns`**:
   - Trong ESP-IDF v6.0.2, mDNS đã được tách thành component riêng biệt trên IDF Component Registry. Dự án bổ sung file cấu hình `main/idf_component.yml` và kế thừa thư viện mDNS chuẩn.
2. **Khởi tạo mDNS Core & Đặt Tên Miền Cục Bộ**:
   - `mdns_init()`: Khởi tạo stack mDNS trên nền LwIP socket UDP cổng 5353 (`224.0.0.251`).
   - `mdns_hostname_set("my_smart_light")`: Đăng ký tên miền `my_smart_light.local`.
   - `mdns_instance_name_set("esp32c3_smart_light")`: Đặt instance name nhận diện thiết bị.
3. **Đăng ký Dịch vụ DNS-SD & Bổ sung TXT Metadata**:
   - Công bố dịch vụ `_http._tcp` cổng 80.
   - Thêm cặp key-value metadata: `board=esp32c3` (hoặc `esp32s3`), `path=/foobar`.
4. **Bảo vệ chống lỗi GCC 15 ICE**:
   - Cấu hình tường minh danh sách `COMPONENTS` và `EXTRA_COMPONENT_DIRS` trong `CMakeLists.txt`.

---

## 3. Hướng Dẫn Kiểm Thử (Testing & Verification)

### Bước 1: Biên dịch và Nạp Firmware
Đảm bảo đã cấu hình đúng thông tin Wi-Fi trong `Kconfig.projbuild` hoặc qua `idf.py menuconfig`.

```powershell
# Chạy build kiểm tra mã nguồn
$paths = @("D:\Espressif\tools\ccache\4.12.1\ccache-4.12.1-windows-x86_64", "D:\Espressif\tools\ninja\1.12.1", "D:\Espressif\tools\cmake\4.0.3\bin", "D:\Espressif\tools\python\v6.0.2\venv\Scripts", "D:\Espressif\tools\riscv32-esp-elf\esp-15.2.0_20251204\riscv32-esp-elf\bin", "D:\Espressif\tools\xtensa-esp-elf\esp-15.2.0_20251204\xtensa-esp-elf\bin", "D:\esp\v6.0.2\esp-idf\tools", "D:\Espressif\tools\idf-exe\1.0.3"); $env:PATH = ($paths -join ";") + ";" + $env:PATH; $env:IDF_PATH = "D:\esp\v6.0.2\esp-idf"; $env:IDF_TOOLS_PATH = "D:\Espressif"; idf.py -C test_case/mdns_discovery build
```

Sau đó flash vào thiết bị:
```powershell
idf.py -C test_case/mdns_discovery -p COM3 flash monitor
```

### Bước 2: Quan sát Log ESP32
Khi khởi động thành công, terminal monitor sẽ in ra:
```text
I (1480) mdns_discovery: ==> [Wi-Fi] KẾT NỐI THÀNH CÔNG! ĐÃ CÓ ĐỊA CHỈ IP: 192.168.1.45
I (1490) mdns_discovery: ==> mDNS Hostname đã thiết lập: [my_smart_light.local]
I (1500) mdns_discovery: ==> mDNS Instance Name: [esp32c3_smart_light]
I (1510) mdns_discovery: ==> Đã công bố dịch vụ mDNS: _http._tcp trên port 80
I (1510) mdns_discovery:     Metadata TXT: board=esp32c3, path=/foobar
I (1520) mdns_discovery: HTTP Server sẵn sàng tại http://my_smart_light.local:80/
```

### Bước 3: Tra cứu mDNS từ Máy Tính

#### Cách 1: Sử dụng công cụ chuẩn `dns-sd` (Nếu có Bonjour Service)
```bash
dns-sd -L esp32c3_smart_light _http
```
Kết quả hiển thị:
```text
Lookup esp32c3_smart_light._http._tcp.local
esp32c3_smart_light._http._tcp.local. can be reached at my_smart_light.local.:80
path=/foobar board=esp32c3
```

#### Cách 2: Ping hoặc mở trình duyệt web
- Trong CMD/Terminal:
  ```bash
  ping my_smart_light.local
  ```
- Hoặc mở trình duyệt truy cập: `http://my_smart_light.local/foobar`. Trình duyệt sẽ hiển thị:
  `PBL5 Smart Light mDNS Discovery OK! Resolved via: my_smart_light.local:80`
  Và thanh LED WS2812B trên bo mạch sẽ nhấp nháy xanh lá xác nhận!

#### Cách 3: Chạy script kiểm thử Python tự động
```bash
python scripts/test_mdns_discovery.py
```
