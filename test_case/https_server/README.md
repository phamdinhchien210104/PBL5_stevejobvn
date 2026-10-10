# Dự Án Thực Hành 8.3 & 8.4: Máy Chủ Web HTTP & HTTPS Cho Đèn Thông Minh

Thư mục: `test_case/https_server`  
Kiến trúc: **PBL5 Multi-Target Smart Light** (ESP32-S3 & ESP32-C3) trên nền **ESP-IDF v6.0.2**

---

## 1. Giới Thiệu
Dự án triển khai máy chủ web nhúng điều khiển đèn thông minh nội bộ (Local Control) qua cả 2 giao thức:
- **HTTP** (Port 80): Giao thức truyền tin không mã hóa tiêu chuẩn.
- **HTTPS** (Port 443): Kênh truyền mã hóa bảo mật với chứng chỉ TLS X.509 (`cacert.pem`) và khóa riêng RSA (`prvtkey.pem`).

### Các tính năng chính:
- **Web Dashboard tương tác trực tiếp (`GET /`)**: Giao diện HTML5 + CSS Glassmorphism + Javascript cho phép người dùng mở trình duyệt trên điện thoại/máy tính để Bật, Tắt, Đổi màu 8 cấp độ, và Kéo thanh trượt điều chỉnh độ sáng từ 5% đến 100%.
- **REST API `/light`**:
  - `GET /light`: Trả về JSON trạng thái đèn (`status`, `brightness`, `color_name`, `rgb`).
  - `POST /light`: Nhận lệnh JSON cập nhật trạng thái đèn.
- **Đa mục tiêu**: Hỗ trợ tự động cả **ESP32-C3** (Boot GPIO 9) và **ESP32-S3** (Boot GPIO 0).
- **Tầng Driver Chuẩn Hóa**: Điều khiển thanh 8 LED WS2812B NeoPixel trên GPIO 4 qua Hardware SPI2 DMA @ 3.2MHz. Nút Boot vật lý hỗ trợ đa cử chỉ (Nhấn 1 lần, Nhấn đúp, Nhấn giữ dimming vô cấp).

---

## 2. Biên Dịch & Nạp Firmware

### Lệnh Biên Dịch:
```powershell
$env:IDF_TOOLS_PATH = "D:\Espressif"; . "D:\esp\v6.0.2\esp-idf\export.ps1"; idf.py -C test_case/https_server build
```

### Lệnh Nạp Firmware & Giám Sát Log:
```powershell
idf.py -C test_case/https_server -p COM3 flash monitor
```

---

## 3. Kiểm Thử

### Cách 1: Qua Trình Duyệt Web
1. Xem địa chỉ IP của ESP32 in ra trên Terminal Monitor (ví dụ: `192.168.1.31`).
2. Mở trình duyệt (Chrome, Safari, Edge) trên cùng mạng Wi-Fi và truy cập:
   ```text
   https://192.168.1.31/
   ```
3. Bấm các nút điều khiển trực tiếp trên màn hình.

### Cách 2: Qua Kịch Bản Python
```powershell
# Chế độ tương tác CLI:
python test_case/https_server/scripts/test_https_server.py --host <IP-ESP32>

# Chế độ tự động Auto-Test:
python test_case/https_server/scripts/test_https_server.py --host <IP-ESP32> --auto
```
