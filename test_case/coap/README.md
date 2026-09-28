# Test Case 7: CoAP & CoAPS (DTLS) Server (`test_case/coap`)

> **Căn cứ tài liệu**: Mục 8.3.4 *Constrained Application Protocol (CoAP)* & Mục 8.4.2 *DTLS* — Sách *ESP32-C3 Wireless Adventure: A Comprehensive Guide to IoT* (trang 185–189, 207–210).  
> **Nền tảng**: ESP-IDF v6.0.2 | GCC 15 | FreeRTOS | libcoap v4.3.x (RFC 7252 / RFC 7641).

---

## 1. So Sánh Kiến Trúc: Mã Nguồn Cũ vs Mã Nguồn Mới Cải Tiến

| Thành phần / Tiêu chí | Mã nguồn gốc trong sách (Cũ) | Mã nguồn cải tiến chuẩn PBL5 (Mới) |
| :--- | :--- | :--- |
| **Giao thức Mạng** | Chỉ hỗ trợ IPv6 thuần túy (gây lỗi trên đa số router Wi-Fi chỉ bật IPv4). | **Hỗ trợ toàn diện IPv4 Socket Endpoint** trên cổng UDP chuẩn `5683`, tương thích 100% mạng gia đình. |
| **Bảo mật CoAPS (DTLS)** | Chưa cấu hình PSK hoặc phụ thuộc thư viện ngoài chưa tích hợp. | **Tích hợp Pre-Shared Key (PSK) DTLS**: Identity `"CoAP"`, Key `"esp32c3_key"`, cấu hình qua mbedTLS của ESP-IDF v6. |
| **Phản hồi dữ liệu GET** | Trả về chuỗi thô sơ `ON` / `OFF`. | **Chuỗi chuẩn hóa JSON RESTful**: `{"status": true/false, "brightness": 100, "color": "Trắng Tinh Khiết"}`. |
| **Điều khiển PUT linh hoạt** | Chỉ nhận chính xác chuỗi `"ON"` hoặc `"OFF"`. | Nhận diện đa dạng: `"ON"`, `"OFF"`, `"toggle"`, `"color"` hoặc JSON `{"status": true}`. |
| **Cơ chế Observable** | Không kích hoạt cờ observer. | **Kích hoạt Observable Pattern (RFC 7641)**: Tự động gửi thông báo trạng thái mới tới Client khi đèn đổi màu/trạng thái. |
| **Hệ thống LED chỉ thị** | PWM LED rời rạc. | **WS2812B Addressable LED Strip (8 hạt)** điều khiển bằng **Hardware SPI2 DMA @ 3.2MHz**. |
| **Nút bấm vật lý HAL** | Đọc GPIO cơ bản không có debounce/gestures. | **IoT Button Driver đa cử chỉ**: Nhấn 1 lần (Bật/Tắt), Nhấn đúp (Đổi 5 màu), Nhấn giữ (Chỉnh độ sáng). |
| **Kiến trúc phần cứng** | Gắn cứng riêng cho ESP32-C3 DevKit. | **Dual-Target linh hoạt**: Tự động nhận diện ESP32-C3 (Boot GPIO 9) và ESP32-S3 (Boot GPIO 0). |

---

## 2. Các Điểm Chỉnh Sửa Cốt Lõi Trên ESP-IDF v6.0.2

1. **Tích hợp Thư viện libcoap qua Managed Component**:
   - Sử dụng phiên bản `espressif/coap` (^4.3.5) tích hợp sẵn trong thư mục `managed_components` với các tùy chọn DTLS mbedTLS (`CONFIG_MBEDTLS_SSL_PROTO_DTLS=y`, `CONFIG_MBEDTLS_PSK_MODES=y`, `CONFIG_MBEDTLS_KEY_EXCHANGE_PSK=y`).
2. **Khởi tạo và Đăng ký Tài nguyên CoAP**:
   - `coap_startup()`
   - `coap_new_context(NULL)`
   - Cấu hình Pre-Shared Key (PSK): `coap_context_set_psk(ctx, "CoAP", (const uint8_t *)psk_key, sizeof(psk_key) - 1)`
   - `coap_new_endpoint(ctx, &serv_addr, COAP_PROTO_UDP)`
   - Tạo URI resource `/light`: `coap_resource_init(coap_make_str_const("light"), 0)`
   - Đăng ký handlers:
     - `coap_register_handler(resource, COAP_REQUEST_GET, esp_coap_get)`
     - `coap_register_handler(resource, COAP_REQUEST_PUT, esp_coap_put)`
   - Kích hoạt Observable: `coap_resource_set_get_observable(resource, 1)`
3. **Bảo vệ chống lỗi GCC 15 ICE**:
   - Khai báo tường minh `COMPONENTS` và `EXTRA_COMPONENT_DIRS` trong `CMakeLists.txt`.

---

## 3. Hướng Dẫn Kiểm Thử (Testing & Verification)

### Bước 1: Biên dịch và Nạp Firmware
Đảm bảo đã cấu hình đúng thông tin Wi-Fi trong `Kconfig.projbuild` hoặc qua `idf.py menuconfig`.

```powershell
# Chạy build kiểm tra mã nguồn
$paths = @("D:\Espressif\tools\ccache\4.12.1\ccache-4.12.1-windows-x86_64", "D:\Espressif\tools\ninja\1.12.1", "D:\Espressif\tools\cmake\4.0.3\bin", "D:\Espressif\tools\python\v6.0.2\venv\Scripts", "D:\Espressif\tools\riscv32-esp-elf\esp-15.2.0_20251204\riscv32-esp-elf\bin", "D:\Espressif\tools\xtensa-esp-elf\esp-15.2.0_20251204\xtensa-esp-elf\bin", "D:\esp\v6.0.2\esp-idf\tools", "D:\Espressif\tools\idf-exe\1.0.3"); $env:PATH = ($paths -join ";") + ";" + $env:PATH; $env:IDF_PATH = "D:\esp\v6.0.2\esp-idf"; $env:IDF_TOOLS_PATH = "D:\Espressif"; idf.py -C test_case/coap build
```

Sau đó flash vào thiết bị:
```powershell
idf.py -C test_case/coap -p COM3 flash monitor
```

### Bước 2: Quan sát Log Khởi Động
```text
I (1450) coap_server: ==> [Wi-Fi] KẾT NỐI THÀNH CÔNG! ĐÃ CÓ ĐỊA CHỈ IP: 192.168.1.45
I (1460) coap_server:   - CoAP Server Port: 5683 (UDP)
I (1470) coap_server:   - Resource URI   : coap://192.168.1.45:5683/light
I (1480) coap_server: CoAP Server đã sẵn sàng phục vụ!
```

### Bước 3: Kiểm Thử Truy Vấn CoAP

#### Cách 1: Chạy Script Python Thuần (Không cần cài thư viện ngoài)
Chạy script có sẵn trong dự án:
```bash
python test_case/coap/scripts/test_coap.py 192.168.1.45
```
Kết quả hiển thị:
```text
====================================================================
  KIỂM THỬ GIAO THỨC NHÚNG CoAP RESTful CLIENT (MỤC 8.3.4)
  Mục tiêu CoAP : coap://192.168.1.45:5683/light
====================================================================

[1] Gửi yêu cầu CoAP GET /light (Đọc trạng thái hiện tại)...
==> Phản hồi GET: Code: 2.05 | Payload: {"status": false, "brightness": 100, "color": "Trắng Tinh Khiết"}

[2] Gửi yêu cầu CoAP PUT /light (Payload: 'ON')...
==> Phản hồi PUT ON: Code: 2.04 | Payload: 

[3] Gửi yêu cầu CoAP PUT /light (Payload: 'color')...
==> Phản hồi PUT COLOR: Code: 2.04 | Payload: 

[4] Gửi yêu cầu CoAP GET /light (Xác nhận trạng thái mới)...
==> Phản hồi GET: Code: 2.05 | Payload: {"status": true, "brightness": 100, "color": "Xanh Lá Cây"}

==> [HOÀN THÀNH KIỂM THỬ] Toàn bộ chuỗi truy vấn CoAP GET/PUT hoạt động hoàn hảo!
```

#### Cách 2: Sử dụng Copper (Cu4Cr) trên Chrome hoặc `coap-client`
- Đọc trạng thái: `coap-client -m get coap://192.168.1.45:5683/light`
- Bật đèn: `coap-client -m put -e "ON" coap://192.168.1.45:5683/light`
- Đổi màu: `coap-client -m put -e "color" coap://192.168.1.45:5683/light`
