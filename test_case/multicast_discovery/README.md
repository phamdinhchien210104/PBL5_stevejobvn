# Test Case 2: Multicast Discovery (`test_case/multicast_discovery`)

> **Căn cứ tài liệu**: Mục 8.2.2 *Multicast* & Mục 8.2.3 — Sách *ESP32-C3 Wireless Adventure: A Comprehensive Guide to IoT* (trang 162–169).  
> **Nền tảng**: ESP-IDF v6.0.2 | GCC 15 | FreeRTOS | LwIP BSD Sockets (IGMP v2/v3).

---

## 1. So Sánh Kiến Trúc: Mã Nguồn Cũ vs Mã Nguồn Mới Cải Tiến

| Thành phần / Tiêu chí | Mã nguồn gốc trong sách (Cũ) | Mã nguồn cải tiến chuẩn PBL5 (Mới) |
| :--- | :--- | :--- |
| **Hệ thống LED chỉ thị** | Điều khiển LED PWM 5 kênh (RGBWC) rời rạc, không có DMA. | **WS2812B Addressable LED Strip (8 hạt)** điều khiển bằng **Hardware SPI2 DMA @ 3.2MHz**. |
| **Phản hồi trực quan** | Chỉ log qua Serial UART. | **Hiệu ứng nhấp nháy LED xanh lá (Green Pulse)** tức thì khi nhận đúng gói tin Discovery. |
| **Nút bấm vật lý HAL** | Đọc GPIO thô sơ, chỉ có thao tác ấn/thả đơn giản. | **IoT Button Driver đa cử chỉ**: Nhấn 1 lần (Bật/Tắt), Nhấn đúp (Đổi 5 màu), Nhấn giữ (Chỉnh độ sáng). |
| **Kiến trúc phần cứng** | Gắn cứng (hardcoded) cho một dòng ESP32-C3 cụ thể. | **Dual-Target linh hoạt**: Tự động nhận diện ESP32-C3 (Boot GPIO 9) và ESP32-S3 (Boot GPIO 0). |
| **Quản lý Wi-Fi** | Wi-Fi Station thô sơ, không cấu hình PMF, mã hóa cũ, dễ timeout. | Cấu hình **WIFI_AUTH_OPEN threshold**, **PMF (Protected Management Frames)**, Mã vùng **VN** (kênh 1-13), băng thông 20MHz chống suy hao sóng. |
| **Quản lý Socket** | Chạy đơn tuyến chặn hàm `app_main` (blocking call). | Tách thành **FreeRTOS Background Task (`multicast_server_task`)**, cấu hình `SO_REUSEADDR`, quản lý giải phóng socket an toàn. |
| **Cấu hình tham số** | Hardcode macro `#define` tĩnh trong mã C. | Tích hợp **Kconfig.projbuild** (dễ dàng cấu hình qua `menuconfig` hoặc biến môi trường). |

---

## 2. Các Điểm Chỉnh Sửa Cốt Lõi Trên ESP-IDF v6.0.2

1. **Gia nhập nhóm Multicast qua IGMP (`IP_ADD_MEMBERSHIP`)**:
   - Sử dụng cấu trúc `struct ip_mreq` và gọi `setsockopt(sockfd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &imreq, sizeof(struct ip_mreq))` để đăng ký nhận các gói tin gửi tới nhóm Class D (`232.10.11.12` hoặc `239.255.255.250`).
2. **Cấu hình TTL (Time-To-Live) cho gói Multicast**:
   - Thiết lập `IP_MULTICAST_TTL = 1` thông qua `setsockopt()`, đảm bảo gói tin Multicast chỉ truyền trong phạm vi subnet cục bộ (Local Subnet / 1 Hop), không bị rò rỉ hay định tuyến ra ngoài Internet.
3. **Chống lỗi GCC 15 Internal Compiler Error (ICE)**:
   - Áp dụng cấu hình tường minh danh sách `COMPONENTS` và `EXTRA_COMPONENT_DIRS` trong `CMakeLists.txt` nhằm kế thừa các driver dùng chung (`light_driver`, `button`, `app_storage`).
4. **Cơ chế phản hồi Unicast**:
   - Khi nhận gói tin truy vấn `"Are you Espressif IOT Smart Light"` gửi đến địa chỉ Multicast của nhóm, ESP32 trích xuất địa chỉ IP và Port thực của Client từ `struct sockaddr_in source_addr` và gửi ngược lại phản hồi unicast: `"ESP32-C3 Smart Light https 443"`.

---

## 3. Hướng Dẫn Kiểm Thử (Testing & Verification)

### Bước 1: Biên dịch và Nạp Firmware
Đảm bảo đã cấu hình đúng thông tin Wi-Fi trong `Kconfig.projbuild` hoặc qua `idf.py menuconfig`.

```powershell
# Chạy build kiểm tra mã nguồn
$paths = @("D:\Espressif\tools\ninja\1.12.1", "D:\Espressif\tools\cmake\4.0.3\bin", "D:\Espressif\tools\python\v6.0.2\venv\Scripts", "D:\Espressif\tools\riscv32-esp-elf\esp-15.2.0_20251204\riscv32-esp-elf\bin", "D:\Espressif\tools\xtensa-esp-elf\esp-15.2.0_20251204\xtensa-esp-elf\bin", "D:\esp\v6.0.2\esp-idf\tools", "D:\Espressif\tools\idf-exe\1.0.3"); $env:PATH = ($paths -join ";") + ";" + $env:PATH; $env:IDF_PATH = "D:\esp\v6.0.2\esp-idf"; $env:IDF_TOOLS_PATH = "D:\Espressif"; idf.py -C test_case/multicast_discovery build
```

Sau đó flash vào thiết bị:
```powershell
idf.py -C test_case/multicast_discovery -p COM3 flash monitor
```

### Bước 2: Quan sát Log ESP32
Khi khởi động và kết nối Wi-Fi thành công, terminal monitor sẽ in ra:
```text
I (1520) multicast_discovery: ==> [Wi-Fi] KẾT NỐI THÀNH CÔNG! ĐÃ CÓ ĐỊA CHỈ IP: 192.168.1.50
I (1530) multicast_discovery: Socket UDP đã bind thành công tới cổng 3333
I (1540) multicast_discovery: ==> [IGMP] ĐÃ GIA NHẬP NHÓM MULTICAST 232.10.11.12 THÀNH CÔNG!
I (1550) multicast_discovery: Sẵn sàng lắng nghe truy vấn Multicast Discovery...
```

### Bước 3: Chạy Kịch Bản Khám Phá Từ Máy Tính (Python Test Script)
Mở một cửa sổ dòng lệnh khác trên máy tính (kết nối cùng Wi-Fi) và thực thi script có sẵn:

```bash
python scripts/test_multicast_discovery.py
```

Kết quả mong đợi trên PC:
```text
====================================================================
  KIỂM THỬ KHÁM PHÁ THIẾT BỊ QUA UDP MULTICAST (MỤC 8.2.2 & 8.2.3)
  Nhóm Multicast : 232.10.11.12
  Cổng UDP       : 3333
  Bản tin truy vấn: 'Are you Espressif IOT Smart Light'
====================================================================

[1] Đang gửi gói tin UDP Multicast tới 232.10.11.12:3333...
[2] Đang chờ phản hồi Unicast từ ESP32...

==> [THÀNH CÔNG] Nhận phản hồi từ thiết bị 192.168.1.50:3333:
    Dữ liệu dịch vụ: 'ESP32-C3 Smart Light https 443'

Kiểm tra bo mạch: Thanh LED WS2812B đã nhấp nháy xanh lá xác nhận!
```
