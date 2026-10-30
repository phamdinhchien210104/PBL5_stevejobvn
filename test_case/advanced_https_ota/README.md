# 11.3.1 — Demo OTA qua Local HTTPS Server

**Mục tiêu:** ESP32-C3 chạy firmware A (`1.0.0`) tải firmware B (`1.1.0`) từ máy tính,
ghi vào phân vùng OTA còn lại, rồi khởi động vào B. Framework mục tiêu: **ESP-IDF v6.0.3**.

```text
Máy tính: Python HTTPS server :8070/firmware.bin
                  ↓ Wi-Fi/LAN
ESP32-C3: esp_https_ota → slot OTA còn lại → reboot → phiên bản mới
```

Đây là firmware thực hành độc lập, không điều khiển đèn/RainMaker. Demo qua Dashboard
được hướng dẫn tại [5_rainmaker/OTA_DEMO.md](../../device_firmware/5_rainmaker/OTA_DEMO.md).

### Mục đích và đối chiếu với sách

Bài này thực hành [mục 11.3.1 — Upgrade Firmware Through a Local Host](https://espressif.github.io/esp32-c3-book-en/chapter_11/11.3/11.3.1.html)
trong *ESP32-C3 Wireless Adventure: A Comprehensive Guide to IoT*. Mục tiêu là
chứng minh thiết bị nhận và chạy firmware được tải qua mạng, thay cho việc phải
nạp mọi phiên bản mới bằng USB. Sách cũng dùng ví dụ `advanced_https_ota` độc lập
ở mục này; chức năng điều khiển đèn không phải thành phần của ví dụ local OTA.

| Nội dung | Ví dụ trong sách | Demo trong repository |
|---|---|---|
| HTTPS server trên máy tính | `openssl s_server`, port 8070 | Python HTTPS server, port 8070 |
| Tạo chứng chỉ | Lệnh OpenSSL | Script tạo CA và chứng chỉ server có SAN khớp IP |
| Kiểm tra hostname/phiên bản | Hướng dẫn bật bỏ qua CN check và version check | Giữ kiểm tra; A và B có phiên bản khác nhau |
| Component trên ESP32 | `esp_https_ota` | `esp_https_ota` trên ESP-IDF v6.0.3 |
| Kích hoạt kiểm tra | OTA task của ví dụ | Task kiểm tra định kỳ, mặc định 30 giây |

Đây là bản điều chỉnh công cụ và cấu hình demo, không phải bản sao nguyên từng
lệnh trong sách. Khi thuyết trình, nêu rõ việc dùng Python HTTPS thay OpenSSL;
nếu giảng viên yêu cầu đúng công cụ OpenSSL, cần dùng công cụ đó theo đề bài.

### Firmware A/B thực chất là gì?

- A (`1.0.0`) và B (`1.1.0`) là hai bản build của cùng project. Chúng chứa chương trình
  kết nối Wi-Fi, chứng chỉ CA, URL và chức năng OTA. Demo hiện tại giữ cùng chức năng,
  thay phiên bản để dễ kiểm chứng cập nhật; B không phải firmware đèn RainMaker.
- `PROJECT_VER` ghi phiên bản vào descriptor bên trong app binary. Đổi tên file `.bin`
  không thay đổi phiên bản. Hai lần build đều tạo `build/advanced_https_ota.bin`;
  lần build B thay file kết quả A trên máy tính, không tự thay chương trình trên ESP32.
- `publish` chỉ copy app binary vào `.local/firmware/firmware.bin` mà server phục vụ,
  chưa ghi gì lên board. Firmware A trên board chủ động tải B qua HTTPS, ghi vào
  `ota_0`, hoàn tất kiểm tra và cập nhật `otadata` để bootloader chọn bản mới sau reboot.
- USB dùng để flash A lần đầu, cấp nguồn và xem Monitor. Dữ liệu cập nhật B truyền
  qua Wi-Fi/HTTPS; không chạy lệnh flash B trong bài kiểm chứng OTA.

**Kết quả kiểm tra trên máy tính:** đã build A/B bằng ESP-IDF v6.0.3, GCC 15.2.0,
target ESP32-C3. App binary 982992 bytes, vừa slot 1 MiB mặc định. Đã tải hai bản
qua Python HTTPS server với xác thực CA/hostname và kiểm tra SHA-256 khớp file gốc;
4 host integration checks đạt. Các kiểm tra này **chưa phải kết quả OTA trên board**.

**Kết quả log phần cứng đã ghi nhận:** ESP32-C3 4 MB chạy A `1.0.0` tại `factory`,
kiểm tra cùng phiên bản, nhận B `1.1.0`, tải đủ 982992 bytes và báo
`OTA SUCCESS. Next boot=ota_0. Restarting...`. Để hoàn chỉnh bằng chứng end-to-end,
cần lưu thêm log sau reboot xác nhận `version=1.1.0` và `partition=ota_0`.
Các log ở các mục hướng dẫn bên dưới là kết quả mong đợi, không thay cho log thực nghiệm.

## 1. Chuẩn bị

- ESP-IDF **v6.0.3** cùng Python environment và toolchain tương ứng đã được cài hoàn chỉnh.
  Mở **ESP-IDF Terminal** trong VS Code, kiểm tra `idf.py --version` trả về `ESP-IDF v6.0.3`.
  Chỉ có thư mục source v6.0.3 chưa đủ; nếu báo thiếu `rich_click` hoặc compiler,
  hoàn tất cài đặt v6.0.3 bằng trình thiết lập ESP-IDF rồi chọn lại môi trường của Extension.
- ESP32-C3, flash tối thiểu 4 MB theo cấu hình demo; xác nhận dung lượng board trước khi flash.
- Máy tính và ESP32 truy cập được nhau qua mạng nội bộ; Wi-Fi cho C3 là 2.4 GHz.
  Máy tính có thể nối LAN. Mạng khách/client isolation có thể chặn kết nối giữa hai máy.
- Tất cả lệnh dưới chạy từ **thư mục gốc repository**. Thay `192.168.1.100` bằng IPv4
  của card mạng máy tính đang dùng, và `COM3` bằng cổng board của bạn.
  Dùng `ipconfig` để xem IP, không chọn địa chỉ VPN/adapter ảo.
- Chọn `🧪 [TC] 11.3.1 - advanced_https_ota` nếu build bằng nút của Extension.
  Các lệnh `idf.py -C ...` dưới đây chọn project bằng đường dẫn tính từ thư mục
  hiện tại của terminal; chọn folder trên thanh trạng thái không tự chuyển terminal đã mở.

### 1.1. Đứng đúng thư mục trước khi chạy lệnh

Mở ESP-IDF Terminal bằng Command Palette trong VS Code, kiểm tra:

```powershell
idf.py --version
Get-Location
```

Version phải là `ESP-IDF v6.0.3`. Nếu terminal đang ở `device_firmware/1_blink`
hoặc ngay trong `test_case/advanced_https_ota`, hãy chuyển về **gốc repository**.
Ví dụ dưới dành cho máy clone repository tại `E:\PBL5\PBL5_stevejobvn`;
mỗi thành viên thay bằng đường dẫn clone thực tế trên máy mình:

```powershell
Set-Location "E:\PBL5\PBL5_stevejobvn"
Get-Location
```

Thực hiện việc này cho từng terminal mới. Các lệnh tương đối trong mục 2–6 và 8
đều dùng thư mục gốc này. Nếu chạy từ `1_blink`, đường dẫn `-C test_case/advanced_https_ota`
sẽ bị nối thành `1_blink/test_case/advanced_https_ota` và báo project không tồn tại.

**Cách khác, giữ thư mục hiện tại và dùng đường dẫn tuyệt đối:**

```powershell
$Project = "E:\PBL5\PBL5_stevejobvn\test_case\advanced_https_ota"
idf.py -C "$Project" set-target esp32c3
idf.py -C "$Project" menuconfig
```

Sau khi cấu hình, có thể dùng cùng biến `$Project` để build và gọi script:

```powershell
idf.py -C "$Project" "-DPROJECT_VER=1.0.0" build
python "$Project\scripts\ota_server.py" publish "$Project\build\advanced_https_ota.bin"
```

Biến chỉ có hiệu lực trong terminal đã khai báo. Không cần chạy cả cách đường dẫn
tương đối và tuyệt đối; chọn một cách nhất quán. Dấu ngoặc kép quanh
`"-DPROJECT_VER=1.0.0"` là cần thiết trong PowerShell.

### 1.2. Chia terminal để demo trực tiếp

| Terminal | Loại | Công việc |
|---|---|---|
| **1 — Firmware A / Monitor** | ESP-IDF v6.0.3 | Cấu hình, build A, publish A, flash A rồi giữ Monitor |
| **2 — HTTPS server** | Python hoặc ESP-IDF Terminal | Chạy `ota_server.py serve`, giữ chạy suốt demo |
| **3 — Firmware B** | ESP-IDF v6.0.3 | Build B và publish B trong khi hai terminal kia vẫn chạy |

Terminal chạy server/Monitor chưa trả lại dấu nhắc là bình thường. Không nhập lệnh
build vào terminal đang chạy server. Mỗi lệnh chạy riêng một dòng; lệnh server kết
thúc bằng từ `serve`, không dán nối thành `servepython`.

## 2. Enabling OTA server / Enabling HTTPS service

Trong terminal Python (cũng có thể dùng ESP-IDF Terminal):

```powershell
python -m pip install -r test_case/advanced_https_ota/scripts/requirements.txt
python test_case/advanced_https_ota/scripts/ota_server.py prepare --host 192.168.1.100
```

Lệnh tạo CA và chứng chỉ server có SAN khớp IP. File được lưu trong `.local/` và đã
được Git bỏ qua. Firmware chỉ nhúng CA công khai, không nhúng khóa riêng.
Giữ lại `.local/certs/`: chạy `prepare` lần nữa sẽ tái sử dụng CA và cấp lại chứng chỉ
server, kể cả khi IP máy tính đổi. Nếu xóa CA và tạo CA mới, phải build/flash lại A
để firmware tin CA mới. Sau khi đổi IP cũng phải cập nhật URL trong firmware.

Mở terminal thứ hai và giữ chạy:

```powershell
python test_case/advanced_https_ota/scripts/ota_server.py serve
```

Server mặc định nghe `0.0.0.0:8070` và chỉ cung cấp `/firmware.bin`.
Ban đầu chưa publish firmware nên trả 404 là bình thường. Nếu Windows Firewall hỏi,
cho Python truy cập mạng Private đang dùng; không cần tắt toàn bộ firewall.

## 3. Cấu hình firmware A

Trong **ESP-IDF Terminal v6.0.3**:

```powershell
idf.py -C test_case/advanced_https_ota set-target esp32c3
idf.py -C test_case/advanced_https_ota menuconfig
```

Trong `Example Connection Configuration`:

- Chọn Wi-Fi, nhập SSID và password của mạng 2.4 GHz.

Trong `Example Configuration`:

- `Firmware Upgrade URL`: `https://192.168.1.100:8070/firmware.bin`
- `Delay between OTA checks`: `30` giây.
- `OTA Receive Timeout`: `15000` ms.

Lưu rồi thoát. Wi-Fi/password được lưu trong `sdkconfig` cục bộ, không đưa lên Git.
Giữ nguyên cấu hình flash 4 MB và partition `Factory app, two OTA definitions` của demo.
Không dùng partition của bài RainMaker cho project này.

## 4. Build và flash A qua USB

```powershell
idf.py -C test_case/advanced_https_ota "-DPROJECT_VER=1.0.0" build
python test_case/advanced_https_ota/scripts/ota_server.py publish test_case/advanced_https_ota/build/advanced_https_ota.bin
idf.py -C test_case/advanced_https_ota -p COM3 flash monitor
```

Lần đầu cần `flash` đầy đủ để nạp bootloader, partition table và app của demo.
Không chạy `erase-flash`: board đã có dữ liệu RainMaker từ bài trước. Tuy nhiên bảng
phân vùng local khác RainMaker, vì vậy vẫn có thể phải provisioning lại khi quay về bài đó.
Nếu cần giữ nguyên board đang dùng RainMaker, làm bài local trên board khác nếu có.

Log mong đợi (partition ban đầu có thể là `factory`):

```text
local_ota: Running project=advanced_https_ota version=1.0.0 ESP-IDF=...
local_ota: Running partition=factory address=...
local_ota: Next OTA check in 30 seconds
local_ota: Offered project=advanced_https_ota version=1.0.0
local_ota: Same version; keeping the running firmware
```

ESP32 kiểm tra định kỳ sau khi kết nối Wi-Fi. Mỗi lần cùng phiên bản sẽ bỏ qua,
không reboot hoặc chuyển slot. Một kết nối có thể đóng sớm sau khi đọc header.

## 5. Build B, chỉ publish — không flash B bằng USB

Giữ server và Monitor đang chạy, mở thêm ESP-IDF Terminal tại repository:

```powershell
idf.py -C test_case/advanced_https_ota "-DPROJECT_VER=1.1.0" build
python test_case/advanced_https_ota/scripts/ota_server.py publish test_case/advanced_https_ota/build/advanced_https_ota.bin
```

Không cần sửa code để đổi phiên bản. Script publish kiểm tra header app rồi thay file
đích bằng rename để tránh phục vụ một file đang copy dở. Chỉ publish **app binary**,
không dùng bootloader, partition table hay merged firmware.

Trong lần kiểm tra tiếp theo:

```text
local_ota: Offered project=advanced_https_ota version=1.1.0
local_ota: Download ...%
local_ota: OTA SUCCESS. Next boot=ota_0. Restarting...
...
local_ota: Running project=advanced_https_ota version=1.1.0 ESP-IDF=...
local_ota: Running partition=ota_0 address=...
```

Lưu log trước/sau và ảnh server có request `GET /firmware.bin` để báo cáo. Nếu muốn
demo lần nữa, dùng `1.2.0`; tên project phải giữ `advanced_https_ota`. Kiểm tra phiên bản
ở đây là **khác nhau**, không phải so sánh lớn/nhỏ theo semantic version.

## 6. Kiểm tra và xử lý lỗi

| Hiện tượng | Kiểm tra |
|---|---|
| Project directory does not exist | Kiểm tra `Get-Location`, về gốc repository hoặc dùng đường dẫn tuyệt đối như mục 1.1 |
| `invalid choice: servepython` | Nhấn Ctrl+C ở dấu nhắc để hủy lệnh đang gõ, nhập lại một dòng `python test_case/advanced_https_ota/scripts/ota_server.py serve` |
| Build báo chưa có CA | Chạy `prepare` trước khi configure/build |
| Không kết nối được Wi-Fi | SSID/password, băng tần 2.4 GHz |
| Timeout/refused | IP/port, server còn chạy, Firewall, client isolation |
| HTTP 404 | Chưa publish hoặc URL khác `/firmware.bin` |
| TLS verify failed | CA trong firmware có đúng với server, SAN khớp IP, ngày giờ máy tính |
| Same version | Đây là kết quả đúng khi server và board cùng phiên bản. Nếu đang mong đợi B, đối chiếu phiên bản đang chạy và log publish; build B bằng `"-DPROJECT_VER=1.1.0"` nếu chưa có |
| Rejected different project | Đang phục vụ firmware của project khác |
| Download failed/incomplete | Giữ A đang chạy, sửa kết nối rồi đợi vòng kiểm tra tiếp theo |

Firmware dùng timestamp lúc build làm giờ TLS khi đồng hồ chưa được đặt, để demo
local không phụ thuộc Internet/NTP. Tạo chứng chỉ **trước** khi build; đây là mốc thời gian
demo, không thay thế đồng bộ thời gian đáng tin cậy cho sản phẩm. Kiểm tra CA/hostname
vẫn bật. Demo không cấu hình secure boot, anti-rollback hay kiểm thử rollback.

Host-side integration checks (Python có `cryptography`):

```powershell
python -m unittest discover -s test_case/advanced_https_ota/scripts -p test_ota_server.py -v
```

Kiểm tra HTTPS thật, xác thực chứng chỉ, đổi firmware khi server vẫn chạy và publish
file sai không làm mất bản trước. Không thay thế kiểm thử OTA trên ESP32.
`example_test.py`, `Makefile`, `component.mk` và các cấu hình CI/anti-rollback cũ là tài liệu
kế thừa ESP-IDF cũ; không dùng chúng cho quy trình demo v6.0.3 này.

## 7. Kịch bản demo trước lớp và tiêu chí đạt

### 7.1. Trình diễn A → B

Chuẩn bị theo mục 1–3, xác nhận đúng IP/COM tại nơi demo. Dùng ba terminal như
mục 1.2; thực hiện build/publish/flash theo mục 4–5.

| Bước | Thao tác trực tiếp | Nội dung trình bày/kết quả cần thấy |
|---|---|---|
| 1 | Build A `1.0.0`, publish A; giữ HTTPS server chạy | Máy tính đang cung cấp firmware nền A |
| 2 | Flash A qua USB, mở Monitor | `version=1.0.0`, `partition=factory` |
| 3 | Đợi kết nối Wi-Fi và khoảng 30 giây | `Offered ... version=1.0.0`, `Same version`; chưa cập nhật |
| 4 | Trong terminal 3, build B `1.1.0`, publish B | Log publish xác nhận `version=1.1.0`; không flash B |
| 5 | Đợi lần kiểm tra tiếp theo | Nhận B, tiến độ tải 0–100%, `OTA SUCCESS` |
| 6 | Quan sát boot log mới | `version=1.1.0`, `partition=ota_0`, địa chỉ `0x00110000` |
| 7 | Đợi thêm một vòng kiểm tra | `Same version`, không cập nhật/reboot lặp lại |

**Tiêu chí pass end-to-end:** có đủ log A trước cập nhật, tải B thành công và chạy B
từ phân vùng OTA sau reboot; B không được flash qua USB. Chỉ `Download 100%` hoặc
`Writing to <ota_0>` chưa đủ để kết luận đã chạy bản mới.

Mẫu bằng chứng cần lưu:

```text
# Trước cập nhật
Running project=advanced_https_ota version=1.0.0 ESP-IDF=v6.0.3
Running partition=factory address=0x00010000

# Trong cập nhật
Offered project=advanced_https_ota version=1.1.0
Download 100% (982992/982992 bytes)
OTA SUCCESS. Next boot=ota_0. Restarting...

# Sau cập nhật
Running project=advanced_https_ota version=1.1.0 ESP-IDF=v6.0.3
Running partition=ota_0 address=0x00110000
```

Kích thước binary có thể khác theo cấu hình build. Lưu log/ảnh thực tế của nhóm;
không dùng log mẫu làm kết quả đã đo. Các dòng Wi-Fi `run -> init`, `flush txq`,
`Deinit...` ngay sau `Restarting...` là quá trình tắt Wi-Fi trước reboot.

Lời giới thiệu ngắn:

> Em thực hành mục 11.3.1 với esp_https_ota. Em flash bản nền 1.0.0 bằng USB,
> sau đó build 1.1.0 và đặt lên HTTPS server trên máy tính. ESP32 tự tải qua Wi-Fi,
> ghi vào phân vùng OTA khác và khởi động bản mới. Demo dùng Python HTTPS server
> thay cho công cụ OpenSSL trong sách, vẫn xác thực chứng chỉ server.

### 7.2. Bảng test case

Đây là bảng kịch bản và **kết quả mong đợi**; người chạy tự ghi pass/fail kèm log.
TC-01 đến TC-03 là phần demo chính. TC-04/05 là kiểm tra lỗi bổ sung, thực hiện
khi thiết bị không đang tải/ghi firmware.

| ID | Kịch bản/cách thử | Kết quả mong đợi |
|---|---|---|
| TC-01 | Board chạy A; server cung cấp A | Cùng phiên bản, bỏ qua cập nhật, tiếp tục vòng kiểm tra |
| TC-02 | Board chạy A; publish B | Tải B, báo thành công, reboot và chạy B ở slot OTA |
| TC-03 | Board đã chạy B; server vẫn cung cấp B | Không cập nhật hoặc reboot lặp lại |
| TC-04 | Dừng server bằng Ctrl+C lúc không cập nhật; đợi một vòng kiểm tra | Báo lỗi kết nối; không đổi phân vùng boot, tiếp tục chương trình hiện tại |
| TC-05 | Sau TC-04, mở lại server; đợi vòng kiểm tra tiếp theo | Truy cập HTTPS trở lại; bỏ qua nếu cùng phiên bản, cập nhật nếu khác phiên bản |

## 8. Đưa về A để demo lại từ đầu

Chỉ reset board sẽ chạy phiên bản đã chọn trong `otadata`, không tự quay về A.
Để trình diễn lại đúng chuỗi `factory/1.0.0 → ota_0/1.1.0`:

1. Trong Monitor, nhấn **Ctrl+]** để thoát và giải phóng cổng COM. Giữ server chạy.
2. Từ gốc repository, trong ESP-IDF Terminal, build và publish lại A:

   ```powershell
   idf.py -C test_case/advanced_https_ota "-DPROJECT_VER=1.0.0" build
   python test_case/advanced_https_ota/scripts/ota_server.py publish test_case/advanced_https_ota/build/advanced_https_ota.bin
   ```

3. **Publish A trước khi flash A** để board không lập tức tải B còn lưu trên server.
   Chạy flash đầy đủ (thay `COM3` bằng cổng thực tế):

   ```powershell
   idf.py -C test_case/advanced_https_ota -p COM3 flash monitor
   ```

   Với cấu hình partition mặc định của demo, lệnh flash đầy đủ nạp lại app factory
   và `ota_data_initial.bin`, đưa lựa chọn boot về factory. Không dùng riêng
   `app-flash` và không cần `erase-flash`. Đối chiếu Monitor phải là A ở factory.

4. Mở terminal 3, thực hiện mục 5 để build/publish B và theo dõi cập nhật.

Giữ nguyên CA, Wi-Fi và URL nếu môi trường không đổi; không cần chạy lại
`set-target` hoặc tạo chứng chỉ cho mỗi lượt demo. Khi đổi mạng/IP, cập nhật URL
và chạy `prepare --host <IP-moi>` để cấp chứng chỉ khớp IP trước khi build/flash A.

## 9. Bàn giao cho nhóm qua GitHub

- Đưa lên mã nguồn, CMake/Kconfig, `sdkconfig.defaults`, README và toàn bộ script
  trong `scripts/`, cùng `.gitignore` của project. Chỉ gửi README là chưa đủ để chạy.
- `.local/`, `build/`, `managed_components/` và `sdkconfig` cục bộ đã được ignore;
  mỗi thành viên tạo chứng chỉ, nhập Wi-Fi/IP/COM và build trên máy mình.
- Ví dụ IP/COM/đường dẫn trong tài liệu không phải giá trị bắt buộc của cả nhóm.
- Hướng dẫn này dùng thư mục clone đang chứa thay đổi OTA. Nếu nhóm lấy nhánh
  `feat/ota-upgrade`, cần checkout đúng nhánh đó, hoặc dùng `main` sau khi PR đã merge.
- Trước commit/push, kiểm tra `git status`, `git diff --check`, `git diff` và đối chiếu
  log demo thực tế. Việc có hướng dẫn và build pass chưa thay thế kiểm thử trên board.
