# Test Case 4: TCP Socket Server & Client (`test_case/tcp_socket`)

> **Căn cứ tài liệu**: Mục 8.3.1 *Transmission Control Protocol (TCP)* — Sách *ESP32-C3 Wireless Adventure: A Comprehensive Guide to IoT* (trang 172–177).  
> **Nền tảng**: ESP-IDF v6.0.2 | GCC 15 | FreeRTOS | LwIP BSD Sockets (RFC 793).

---

## 1. So Sánh Kiến Trúc: Mã Nguồn Cũ vs Mã Nguồn Mới Cải Tiến

| Thành phần / Tiêu chí | Mã nguồn gốc trong sách (Cũ) | Mã nguồn cải tiến chuẩn PBL5 (Mới) |
| :--- | :--- | :--- |
| **Mặc định Role** | Mặc định chạy `TCP_CLIENT` kết nối tới IP tĩnh `192.168.3.80` (crash/treo nếu không có server). | Mặc định chạy **`TCP_SERVER` (Smart Light)** sẵn sàng nhận kết nối điều khiển từ App/PC. |
| **Xử lý lệnh** | Chỉ in log chuỗi nhận được, không thực sự điều khiển đèn. | **Tích hợp bộ parser lệnh phong phú**: Hỗ trợ `"Open the light"`, `"Close the light"`, `"Toggle"`, `"Color"`, `"Status"` và phản hồi lại kết quả qua socket. |
| **Hệ thống LED chỉ thị** | PWM LED 5 kênh rời rạc không có phản hồi lệnh. | **WS2812B Addressable LED Strip (8 hạt)** điều khiển bằng **Hardware SPI2 DMA @ 3.2MHz**, thay đổi trạng thái và màu sắc ngay khi nhận lệnh TCP. |
| **Chống Zombie Client** | Có keep-alive cơ bản nhưng hàm `app_main` bị chặn nghẽn. | Cấu hình **TCP Keep-Alive chuyên sâu** (`SO_KEEPALIVE`, `TCP_KEEPIDLE=5s`, `TCP_KEEPINTVL=3s`, `TCP_KEEPCNT=3`), chạy trong **FreeRTOS Task riêng biệt**. |
| **Nút bấm vật lý HAL** | Đọc GPIO cơ bản không có debounce/gestures. | **IoT Button Driver đa cử chỉ**: Nhấn 1 lần (Bật/Tắt), Nhấn đúp (Đổi 5 màu), Nhấn giữ (Chỉnh độ sáng). |
| **Kiến trúc phần cứng** | Gắn cứng riêng cho ESP32-C3 DevKit. | **Dual-Target linh hoạt**: Tự động nhận diện ESP32-C3 (Boot GPIO 9) và ESP32-S3 (Boot GPIO 0). |
| **Quản lý Wi-Fi** | Wi-Fi Station cơ bản, không có PMF hay cấu hình bảo mật hiện đại. | Cấu hình **WIFI_AUTH_OPEN threshold**, **PMF**, Mã vùng **VN**, băng thông 20MHz ổn định. |

---

## 2. Các Điểm Chỉnh Sửa Cốt Lõi Trên ESP-IDF v6.0.2

1. **Khởi tạo TCP Server chuẩn POSIX/BSD Socket**:
   - `socket(AF_INET, SOCK_STREAM, IPPROTO_IP)`
   - `setsockopt(SOL_SOCKET, SO_REUSEADDR)` cho phép tái sử dụng cổng ngay sau khi reset socket.
   - `bind()` tới cổng `CONFIG_TCP_PORT` (3333).
   - `listen()` và `accept()`.
2. **Cơ chế TCP Keep-Alive chống treo bộ nhớ (Zombie Connections)**:
   - Khi smartphone bị ngắt kết nối đột ngột (đi ra khỏi vùng Wi-Fi hoặc tắt máy), nếu không có keep-alive socket sẽ bị treo vĩnh viễn (Leak socket descriptor).
   - Thiết lập `TCP_KEEPIDLE=5`, `TCP_KEEPINTVL=3`, `TCP_KEEPCNT=3` đảm bảo sau tối đa 14 giây mất tín hiệu, ESP32 sẽ tự động thu hồi tài nguyên socket.
3. **Chống lỗi GCC 15 ICE**:
   - Khai báo tường minh `COMPONENTS` và `EXTRA_COMPONENT_DIRS` trong `CMakeLists.txt`.

---

## 3. Hướng Dẫn Kiểm Thử (Testing & Verification)

### Bước 1: Biên dịch và Nạp Firmware
Đảm bảo đã cấu hình đúng thông tin Wi-Fi trong `Kconfig.projbuild` hoặc qua `idf.py menuconfig`.

```powershell
# Chạy build kiểm tra mã nguồn
$paths = @("D:\Espressif\tools\ccache\4.12.1\ccache-4.12.1-windows-x86_64", "D:\Espressif\tools\ninja\1.12.1", "D:\Espressif\tools\cmake\4.0.3\bin", "D:\Espressif\tools\python\v6.0.2\venv\Scripts", "D:\Espressif\tools\riscv32-esp-elf\esp-15.2.0_20251204\riscv32-esp-elf\bin", "D:\Espressif\tools\xtensa-esp-elf\esp-15.2.0_20251204\xtensa-esp-elf\bin", "D:\esp\v6.0.2\esp-idf\tools", "D:\Espressif\tools\idf-exe\1.0.3"); $env:PATH = ($paths -join ";") + ";" + $env:PATH; $env:IDF_PATH = "D:\esp\v6.0.2\esp-idf"; $env:IDF_TOOLS_PATH = "D:\Espressif"; idf.py -C test_case/tcp_socket build
```

Sau đó flash vào thiết bị:
```powershell
idf.py -C test_case/tcp_socket -p COM3 flash monitor
```

### Bước 2: Quan sát Log Khởi Động
```text
I (1460) tcp_socket: ==> [Wi-Fi] KẾT NỐI THÀNH CÔNG! ĐÃ CÓ ĐỊA CHỈ IP: 192.168.1.45
I (1470) tcp_socket:   - TCP Server Port: 3333
I (1480) tcp_socket: TCP Server đã sẵn sàng lắng nghe kết nối tại cổng 3333
```

### Bước 3: Điều Khiển Qua TCP

#### Cách 1: Sử dụng công cụ Netcat (`nc`) hoặc PuTTY
Mở CMD / Terminal trên máy tính:
```bash
# Cú pháp: nc <ESP32_IP> 3333
nc 192.168.1.45 3333
```
Sau đó gõ các lệnh:
- `Open the light` $\rightarrow$ Đèn bật sáng, trả về: `+OK: Light is ON | Brightness: 100% | Color: Trắng Tinh Khiết`
- `Color` $\rightarrow$ Đèn chuyển màu (Đỏ / Xanh lá / Xanh dương / Vàng...), trả về: `+OK: Switched color to Xanh Lá Cây`
- `Close the light` $\rightarrow$ Đèn tắt, trả về: `+OK: Light is OFF`
- `Status` $\rightarrow$ Trả về thông tin trạng thái hiện tại.

#### Cách 2: Chạy Script Python Tự Động
```bash
python test_case/tcp_socket/scripts/test_tcp_socket.py 192.168.1.45
```
Kịch bản Python sẽ tự động bắt tay TCP, lần lượt gửi 5 lệnh kiểm thử và kiểm tra kết quả phản hồi từ ESP32.
