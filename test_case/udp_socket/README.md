# Test Case 5: UDP Socket Server & Client (`test_case/udp_socket`)

> **Căn cứ tài liệu**: Mục 8.3.3 *User Datagram Protocol (UDP)* — Sách *ESP32-C3 Wireless Adventure: A Comprehensive Guide to IoT* (trang 182–185).  
> **Nền tảng**: ESP-IDF v6.0.2 | GCC 15 | FreeRTOS | LwIP BSD Sockets (RFC 768).

---

## 1. So Sánh Kiến Trúc: Mã Nguồn Cũ vs Mã Nguồn Mới Cải Tiến

| Thành phần / Tiêu chí | Mã nguồn gốc trong sách (Cũ) | Mã nguồn cải tiến chuẩn PBL5 (Mới) |
| :--- | :--- | :--- |
| **Mặc định Role** | Mặc định chạy `UDP_CLIENT` gửi tới IP cứng `192.168.3.80` (vô ích nếu không có server). | Mặc định chạy **`UDP_SERVER` (Smart Light)** sẵn sàng nhận datagram từ bất kỳ host nào trong LAN. |
| **Cơ chế Phản hồi** | Chỉ nhận và in log một chiều, không có ACK. | Triển khai **Application-level ACK**: Gửi phản hồi `+OK: Open the light OK...` qua `sendto()` xác nhận trạng thái. |
| **Xử lý lệnh** | Không thay đổi trạng thái phần cứng đèn. | **Tích hợp bộ parser lệnh phong phú**: Hỗ trợ `"Open the light"`, `"Close the light"`, `"Toggle"`, `"Color"`, `"Status"`. |
| **Hệ thống LED chỉ thị** | PWM LED 5 kênh rời rạc không có phản hồi lệnh. | **WS2812B Addressable LED Strip (8 hạt)** điều khiển bằng **Hardware SPI2 DMA @ 3.2MHz**, thay đổi trạng thái và màu sắc ngay khi nhận gói tin UDP. |
| **Quản lý Socket** | Thực thi đơn tuyến chặn hàm `app_main`. | Tách thành **FreeRTOS Background Task (`udp_server_task`)**, cấu hình `SO_REUSEADDR`, quản lý giải phóng socket an toàn. |
| **Nút bấm vật lý HAL** | Đọc GPIO cơ bản không có debounce/gestures. | **IoT Button Driver đa cử chỉ**: Nhấn 1 lần (Bật/Tắt), Nhấn đúp (Đổi 5 màu), Nhấn giữ (Chỉnh độ sáng). |
| **Kiến trúc phần cứng** | Gắn cứng riêng cho ESP32-C3 DevKit. | **Dual-Target linh hoạt**: Tự động nhận diện ESP32-C3 (Boot GPIO 9) và ESP32-S3 (Boot GPIO 0). |
| **Quản lý Wi-Fi** | Wi-Fi Station cơ bản, không có PMF hay cấu hình bảo mật hiện đại. | Cấu hình **WIFI_AUTH_OPEN threshold**, **PMF**, Mã vùng **VN**, băng thông 20MHz ổn định. |

---

## 2. Các Điểm Chỉnh Sửa Cốt Lõi Trên ESP-IDF v6.0.2

1. **Khởi tạo Socket UDP không kết nối (Connectionless Datagram)**:
   - `socket(AF_INET, SOCK_DGRAM, IPPROTO_IP)`
   - Cấu hình `setsockopt(SOL_SOCKET, SO_REUSEADDR)` giải phóng cổng nhanh chóng.
   - `bind()` socket tới cổng `CONFIG_UDP_PORT` (3333).
2. **Cơ chế xác nhận tầng ứng dụng (Application-level ACK)**:
   - Vì giao thức UDP không có cơ chế ACK ở tầng truyền vận (Transport Layer), nên ứng dụng Smart Light tự trích xuất `struct sockaddr_in source_addr` của Client từ `recvfrom()` và phản hồi gói tin trạng thái bằng `sendto()`.
3. **Bảo vệ chống lỗi GCC 15 ICE**:
   - Khai báo tường minh `COMPONENTS` và `EXTRA_COMPONENT_DIRS` trong `CMakeLists.txt`.

---

## 3. Hướng Dẫn Kiểm Thử (Testing & Verification)

### Bước 1: Biên dịch và Nạp Firmware
Đảm bảo đã cấu hình đúng thông tin Wi-Fi trong `Kconfig.projbuild` hoặc qua `idf.py menuconfig`.

```powershell
# Chạy build kiểm tra mã nguồn
$paths = @("D:\Espressif\tools\ccache\4.12.1\ccache-4.12.1-windows-x86_64", "D:\Espressif\tools\ninja\1.12.1", "D:\Espressif\tools\cmake\4.0.3\bin", "D:\Espressif\tools\python\v6.0.2\venv\Scripts", "D:\Espressif\tools\riscv32-esp-elf\esp-15.2.0_20251204\riscv32-esp-elf\bin", "D:\Espressif\tools\xtensa-esp-elf\esp-15.2.0_20251204\xtensa-esp-elf\bin", "D:\esp\v6.0.2\esp-idf\tools", "D:\Espressif\tools\idf-exe\1.0.3"); $env:PATH = ($paths -join ";") + ";" + $env:PATH; $env:IDF_PATH = "D:\esp\v6.0.2\esp-idf"; $env:IDF_TOOLS_PATH = "D:\Espressif"; idf.py -C test_case/udp_socket build
```

Sau đó flash vào thiết bị:
```powershell
idf.py -C test_case/udp_socket -p COM3 flash monitor
```

### Bước 2: Quan sát Log Khởi Động
```text
I (1450) udp_socket: ==> [Wi-Fi] KẾT NỐI THÀNH CÔNG! ĐÃ CÓ ĐỊA CHỈ IP: 192.168.1.45
I (1460) udp_socket:   - UDP Server Port: 3333
I (1470) udp_socket: UDP Server đã sẵn sàng lắng nghe datagram tại cổng 3333
```

### Bước 3: Điều Khiển Qua UDP

#### Cách 1: Sử dụng Netcat (`nc`) ở chế độ UDP
```bash
# Bắn datagram bật đèn
echo "Open the light" | nc -u -w1 192.168.1.45 3333

# Bắn datagram đổi màu
echo "Color" | nc -u -w1 192.168.1.45 3333

# Bắn datagram tắt đèn
echo "Close the light" | nc -u -w1 192.168.1.45 3333
```

#### Cách 2: Chạy Script Python Tự Động
```bash
python scripts/test_udp_socket.py 192.168.1.45
```
Kịch bản Python sẽ gửi chuỗi 5 lệnh và xác nhận gói tin ACK phản hồi từ đèn.
