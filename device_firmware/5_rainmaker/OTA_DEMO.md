# 11.3.2 — Demo cập nhật qua ESP RainMaker Dashboard

**Framework mục tiêu:** ESP-IDF v6.0.3, **target:** ESP32-C3.
Mục tiêu: flash firmware A (`1.0.0`), dùng Dashboard cập nhật sang B (`1.1.0`),
thiết bị kết nối lại, vẫn điều khiển được đèn và có thêm hiệu ứng **Police** đỏ–xanh.

**Kết quả kiểm tra build:** A/B đã build bằng ESP-IDF v6.0.3, GCC 15.2.0,
target ESP32-C3, component RainMaker được resolve tại lần kiểm tra là v1.17.0.
Binary A 1706592 bytes, B 1707072 bytes; đều vừa slot 1920 KiB (còn khoảng 13%).
Đã đối chiếu descriptor và danh sách hiệu ứng trong ELF: A có bốn hiệu ứng,
B có thêm Police; callback timer Police không có trong A và có trong B.
**Kết quả thực hành:** người chạy đã xác nhận cập nhật B qua Dashboard và kiểm tra
Police trên app/đèn thành công. Chưa lưu đầy đủ bằng chứng Dashboard/log sau reboot
trong repository, và chưa xác nhận toàn bộ bảng kiểm thử hồi quy ở mục 6. Khi nhóm
chạy lại, tự ghi kết quả thực tế và chụp ảnh trước/sau theo hướng dẫn dưới đây.

Tài liệu bám [mục 11.3.2 — Upgrade Firmware Through ESP RainMaker](https://espressif.github.io/esp32-c3-book-en/chapter_11/11.3/11.3.2.html)
trong *ESP32-C3 Wireless Adventure: A Comprehensive Guide to IoT*. Thiết bị cần Internet;
máy tính upload firmware không bắt buộc cùng LAN với ESP32. Bài này không dùng
Python HTTPS server hoặc script `publish` của bài local OTA.

## A và B khác nhau ở chức năng nào?

| Nội dung | A — `1.0.0` | B — `1.1.0` |
|---|---|---|
| Hiệu ứng | Static, Blink, Breathing, Rainbow | Giữ bốn hiệu ứng cũ và thêm **Police** |
| Bật/tắt, độ sáng, Hue/Saturation, nút bấm | Các chức năng hiện có | Tiếp tục sử dụng các chức năng hiện có |
| Tùy chọn build | `APP_ENABLE_POLICE_EFFECT=OFF` | `APP_ENABLE_POLICE_EFFECT=ON` |
| Log kiểm tra khi boot | `Police effect: disabled` | `Police effect: enabled` |

Police đổi toàn bộ dải LED giữa **đỏ và xanh dương mỗi 250 ms**, theo mức Brightness.
Brightness bằng 0 thì tối; Power OFF dừng hiệu ứng, Power ON khôi phục chế độ đã chọn
trong phiên chạy. Chuyển về Static hoặc hiệu ứng khác sẽ dừng Police. Hue/Saturation
vẫn chỉnh được nhưng không đổi hai màu cố định của Police; chúng dùng lại khi về Static.
Sau reboot, chế độ mặc định của ứng dụng vẫn là Static, nên người demo chủ động chọn Police.

Tùy chọn build cho phép tái tạo A/B từ cùng mã nguồn, không phải sửa tay rồi xóa code
mỗi lượt demo. Khi OFF, cả tên lựa chọn và callback Police bị loại khỏi bản build.
`PROJECT_VER` chỉ đổi phiên bản, **không tự bật tính năng**. Tùy chọn CMake được cache
theo thư mục build, vì vậy luôn truyền rõ OFF cho A và ON cho B như các lệnh dưới.

Để tích hợp hiệu ứng mới, phần xử lý chọn chế độ/Power và bảo vệ timer có được chỉnh
trong `app_driver.c`; danh sách gửi RainMaker lấy cùng nguồn với driver, và yêu cầu
hiệu ứng không hỗ trợ được từ chối. Đây là bổ sung trên các chức năng cũ, nhưng vẫn
phải kiểm tra hồi quy các chế độ cũ theo bảng ở mục 6 trước khi kết luận trên phần cứng.

## 1. Kiểm tra Dashboard trước

1. Mở [RainMaker Dashboard](https://dashboard.rainmaker.espressif.com/), đăng nhập
   bằng cùng tài khoản đã thêm đèn vào ứng dụng RainMaker.
2. Kiểm tra node của đèn. Đối chiếu Node ID trên app với Dashboard; trạng thái cần online.
3. Kiểm tra có quyền vào phần firmware/OTA và chọn node làm đích OTA hay không.
   Giao diện **ESP RainMaker Classic** đã dùng có các mục bên trái:
   **Nodes**, **Firmware images**, **OTA jobs**. Tên menu có thể thay đổi theo phiên bản.
4. Sau khi flash A ở mục 3 và kết nối thành công, trong **Nodes** phải thấy node
   `ESP RainMaker Smart Light`, trạng thái **Online**, Type **Lightbulb**,
   Model **5_rainmaker**, Version **1.0.0**. Đối chiếu Node ID nếu có nhiều board;
   không chọn node chỉ dựa vào tên vì tên các board có thể giống nhau.

**Có quyền điều khiển đèn trong app chưa chắc có quyền tạo OTA task.** Theo API của
RainMaker, Dashboard OTA yêu cầu admin của node. Với public deployment và node dùng
Self Claiming, primary user tự trở thành admin. Project hiện dùng BLE Assisted Claiming;
cần kiểm tra quyền thực tế, không suy ra quyền admin từ việc điều khiển đèn thành công.
Nếu không thấy node hoặc không chọn được node cho OTA, ghi lại màn hình/thông báo
và xác định tài khoản quản trị/quyền truy cập trước khi tiếp tục. Không factory reset
hay đổi claiming chỉ để thử tìm node.

## 2. Firmware thay đổi gì?

```c
ESP_ERROR_CHECK(esp_rmaker_ota_enable_default());
```

Helper này bật **OTA_USING_TOPICS**, đúng quy trình OTA task trên Dashboard:

```text
Dashboard tạo OTA task → Cloud thông báo qua MQTT topics
                         ↓
ESP32 tải app .bin qua HTTPS → ghi slot OTA còn lại → reboot
                         ↓
Kết nối lại RainMaker → kiểm tra phiên bản và điều khiển đèn
```

`OTA_USING_PARAMS` trước đây là workflow khác. Topics là kênh nhận yêu cầu, không
có nghĩa demo đang truyền file firmware qua MQTT. HTTPS dùng certificate bundle;
không dùng chứng chỉ local của bài 11.3.1.

Mặc định trong `sdkconfig.defaults`:

- HTTPS OTA; xác thực bằng certificate bundle.
- Auto fetch một lần khi kết nối cloud sau boot (`AUTOFETCH_PERIOD=0`).
- Báo tiến độ theo bước 10%; tự reboot sau cập nhật theo mặc định RainMaker.
- Giữ project name là `5_rainmaker`; hai slot OTA hiện có, mỗi slot 1920 KiB.

**Nếu đã có `sdkconfig` từ bài trước**, defaults mới không tự ghi đè mọi giá trị.
Mở `menuconfig` và kiểm tra trong `ESP RainMaker Config`:

- `Use Certificate Bundle`: bật.
- `ESP RainMaker OTA Config` → protocol HTTPS, Auto Fetch bật, period `0`.
- Bật progress support, interval `10` nếu có tùy chọn này trong bản component đã resolve.
- `Disable auto reboot`, skip version check và skip project name check: tắt.
- Trong cấu hình mbedTLS, certificate bundle phải bật.
- Trong `Component config` → `Protocomm`, bật Security Version 1
  (`CONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_1=y`); `app_wifi` hiện dùng
  security 1 để provisioning. Thiếu tùy chọn này sẽ báo `NETWORK_PROV_SECURITY_1 undeclared`.

Không thay đổi partition table hoặc bật anti-rollback cho demo này.

## 3. Build và flash A

Mở ESP-IDF Terminal, xác nhận `idf.py --version` là v6.0.3.
Chạy từ thư mục gốc repository, thay `COM3` bằng cổng board của từng thành viên.
Ví dụ trên máy clone tại `E:\PBL5\PBL5_stevejobvn`:

```powershell
Set-Location "E:\PBL5\PBL5_stevejobvn"
Get-Location
idf.py --version
```

Mỗi terminal mới cần đứng đúng thư mục. Nếu terminal đang ở `device_firmware/1_blink`
hoặc `device_firmware/5_rainmaker`, hãy về gốc trước khi dùng các lệnh tương đối sau.
Có thể thay `-C device_firmware/5_rainmaker` bằng đường dẫn tuyệt đối đến project
trên máy mình. Không dùng `-C "$Project"` khi chưa khai báo biến trong terminal đó.

Nếu Monitor của bài khác còn giữ cổng USB, thoát bằng **Ctrl+]** trước khi flash.
Chạy lần lượt, đợi build thành công rồi mới flash:

```powershell
idf.py -C device_firmware/5_rainmaker set-target esp32c3
idf.py -C device_firmware/5_rainmaker menuconfig
idf.py -C device_firmware/5_rainmaker "-DPROJECT_VER=1.0.0" "-DAPP_ENABLE_POLICE_EFFECT=OFF" build
idf.py -C device_firmware/5_rainmaker -p COM3 flash monitor
```

Không xóa flash hoặc nhấn giữ Boot (factory reset) nếu muốn giữ thông tin provisioning.
Nếu đã chạy bài local với partition khác, có thể cần thêm lại thiết bị trên app. Làm theo
QR/provisioning của project, kiểm tra đúng node sau khi kết nối lại.

Log khởi động cần có:

```text
rainmaker: Running project=5_rainmaker version=1.0.0 ESP-IDF=... partition=ota_0
rainmaker: Police effect: disabled
rainmaker: Enabling RainMaker Dashboard OTA (topics + HTTPS)...
```

Tên partition thực tế có thể khác tùy lịch sử cập nhật. Chờ log MQTT/cloud kết nối,
kiểm tra bật/tắt đèn trên app trước khi demo. Dòng `initialized successfully` trong
app hiện tại không tự chứng minh đã kết nối cloud; cần đối chiếu log MQTT và app.
Mở mục Effect trên app: A chỉ có Static, Blink, Breathing, Rainbow. Lưu ảnh danh sách
này trước khi gửi OTA để có bằng chứng tính năng mới chưa tồn tại trong A.

## 4. Build B — không flash B qua USB

Giữ A đang chạy, server local không cần cho bài này. Trong terminal khác:

```powershell
idf.py -C device_firmware/5_rainmaker "-DPROJECT_VER=1.1.0" "-DAPP_ENABLE_POLICE_EFFECT=ON" build
```

File upload:

```text
device_firmware/5_rainmaker/build/5_rainmaker.bin
```

Chỉ upload app binary này. Không dùng `bootloader.bin`, `partition-table.bin`, merged
firmware hoặc binary từ project local OTA. Build phải qua kiểm tra kích thước slot.
Hai lần build mặc định tạo cùng đường dẫn; sau khi build B, file ở đây là B,
trong khi board vẫn chạy A đã flash trước đó. Không chạy lệnh flash sau bước build B.

### Kiểm tra đúng file B trước khi upload

Trong terminal vừa build B, chạy:

```powershell
python -m esptool --chip esp32c3 image-info "device_firmware/5_rainmaker/build/5_rainmaker.bin"
```

Ở phần **Application Information** phải có:

```text
Project name: 5_rainmaker
App version: 1.1.0
ESP-IDF: v6.0.3
```

Tùy chọn build đã lưu trong `device_firmware/5_rainmaker/build/CMakeCache.txt` cần là
`APP_ENABLE_POLICE_EFFECT:BOOL=ON`. Đây là file sinh ra khi build, không chỉnh tay.
Kích thước B tại lần kiểm tra là **1707072 bytes**, A là **1706592 bytes**; kích thước
chỉ dùng tham khảo vì có thể thay đổi theo cấu hình. Descriptor bên trong binary
mới là nguồn kiểm chứng phiên bản, không phải tên file hoặc tên image trên Dashboard.

Nếu file vẫn là `1.0.0` hoặc Police OFF, chạy lại đúng lệnh build B ở trên và kiểm tra
lại trước khi upload. Bấm Build qua Command Palette có thể giữ cache cũ hoặc build
nhầm project. Không cần flash hoặc xóa flash để xử lý nhầm file build.

## 5. Upload firmware và Start OTA

Giữ nguồn và Internet của board; có thể giữ Serial Monitor ở terminal 1 để chẩn đoán.
Dùng terminal 2 để build B; thao tác upload/tạo task trong trình duyệt. Không nhập lệnh
build vào Monitor và không dùng nút Flash sau khi build B.

### 5.1. Firmware images → Add firmware image

1. Trong menu trái, bấm dấu **+ cạnh Firmware images** để mở **Add firmware image**.
2. Ở **Basic details**, điền/chọn:

   | Trường | Giá trị cho demo |
   |---|---|
   | **Image name** | `SmartLight-1.1.0-Police-B` (tên để phân biệt image mới) |
   | **Type** | `Lightbulb` (khớp Type của node) |
   | **Select firmware image file / Browse** | Chọn `device_firmware/5_rainmaker/build/5_rainmaker.bin` vừa kiểm tra ở mục 4 |

   File đã xuất hiện trong biểu mẫu chỉ có nghĩa đã chọn file; cần hoàn tất nút
   xác nhận bên dưới để thêm image vào danh sách.

3. Ở **Secure signing**, chọn **Do not securely sign this image** cho firmware demo
   hiện tại chưa cấu hình ký Secure Boot.
4. Ở **Firmware version & model**, **để Dashboard đọc từ image**: để trống hai ô override
   nếu chưa được tự điền. Nếu Dashboard hiển thị giá trị đọc được, đối chiếu:

   ```text
   Firmware version: 1.1.0
   Model: 5_rainmaker
   ```

   Không nhập `Police` vào Model: Police là tên hiệu ứng, còn model/project vẫn là
   `5_rainmaker`. Nhập `1.1.0` ở ô override không sửa firmware A thành B; thiết bị
   vẫn kiểm tra phiên bản thực bên trong file tải về.
5. **Upload this image as raw:** không chọn, vì đang dùng ESP-IDF application binary
   có descriptor để hệ thống đọc version/model.
6. Cuộn cuối biểu mẫu, bấm nút xác nhận thêm/upload image theo giao diện. Chờ hoàn tất,
   quay lại **Firmware images**, kiểm tra image mới có Version **1.1.0**, Model **5_rainmaker**.

### 5.2. Chọn image B → Start OTA

1. Mở image B vừa upload hoặc menu thao tác **⋮** của image đó, chọn **Start OTA**.
2. Ở **Basic details**, đặt tên task nếu biểu mẫu yêu cầu, ví dụ `demo-police-1.1.0`;
   mô tả có thể dùng `Add Police lighting effect`.
3. Mở **Firmware image** để đối chiếu đúng image B `1.1.0` / `5_rainmaker`. Nếu mục
   đang thu gọn chỉ hiển thị ID image, mở rộng ra; không chọn nhầm image A trước đó.
4. Ở **Nodes**, chọn **Select from your nodes**, mở danh sách **Nodes** và chọn node
   `ESP RainMaker Smart Light` đúng Node ID, đang Online và chạy `1.0.0`.
   Chỉ chọn board đang demo; không cần **Select from your node groups**.
5. Ở **Force push**, bật **Force push this OTA job**.
6. Với demo cập nhật ngay, không yêu cầu **User approval**, không bật thêm **Secure boot**
   hoặc **Host MCU**, không đặt lịch ở **Apply at device time**. Các mục nâng cao như
   priority, validity period, custom metadata, dynamic OTA và network serialised
   giữ mặc định của biểu mẫu, không cần tự thêm giá trị cho demo một node.
7. Cuộn xuống cuối và bấm nút tạo/khởi chạy OTA job theo giao diện.

### 5.3. OTA jobs → theo dõi tác vụ

Mở **OTA jobs**, chọn task vừa tạo và xem trạng thái của đúng node. Quá trình dự kiến
đi từ đang cập nhật (**in-progress**) đến thành công (**success**). Nếu job chỉ mới tạo,
chưa coi là thiết bị đã cập nhật; nếu **rejected** hoặc **failed**, xem lý do rồi xử lý.
Sau đó kiểm chứng bản đang chạy và tính năng theo mục 6.

Force Push gửi thông báo ngay, không bỏ qua xác thực TLS hay kiểm tra firmware.
Nếu không chọn, task có thể chờ auto-fetch theo policy. Với period `0` của demo,
node chủ động fetch khi kết nối cloud lần đầu sau boot; không có polling định kỳ.

## 6. Bằng chứng demo

### 6.1. Kiểm chứng nhanh bằng Dashboard và app

Không cần tìm toàn bộ log dài để trình diễn. Sau khi chạy task:

1. **Dashboard → Nodes:** tải lại trang, xác nhận đúng Node ID, trạng thái **Online**
   và Version đổi từ **1.0.0 → 1.1.0**.
2. **App ESP RainMaker:** mở lại màn hình đèn, vào **Effect**. Nếu còn cache giao diện
   cũ, tải lại danh sách thiết bị hoặc đóng/mở lại app. Danh sách B cần có **Police**.
3. Bật **Power**, đặt **Brightness 25–50%**, chọn **Police** và quan sát đỏ–xanh luân phiên.
4. Tắt/bật Power rồi đổi về Static; kiểm tra thêm các chế độ cũ theo bảng hồi quy bên dưới.

**Tiêu chí đạt demo:** A trước đó là `1.0.0` chưa có Police; sau OTA, Dashboard báo node
Online ở `1.1.0`, app có Police và đèn thực sự chạy hiệu ứng. B không được flash qua USB.
Lưu thêm ảnh chi tiết OTA job báo **success** nếu có để chứng minh đường cập nhật cloud.
Việc chỉ đổi nhãn image/version trong Dashboard hoặc chỉ thấy tên Police trong app
chưa đủ thay cho kiểm tra phiên bản node và hoạt động thật của đèn.

### 6.2. Bằng chứng nên lưu khi báo cáo

- Ảnh **Nodes trước OTA**: node online, Version `1.0.0` và ảnh app có bốn hiệu ứng.
- Ảnh Dashboard firmware B và OTA task với đúng node.
- Ảnh **Nodes sau OTA**: node online, Version `1.1.0`; ảnh app có Police và video đèn
  đỏ–xanh; ảnh trạng thái **success** của node trong OTA job nếu có.
- Log A (`version=1.0.0`) trước cập nhật.
- Log OTA nhận yêu cầu/tải firmware/reboot.
- Log sau reboot: `project=5_rainmaker version=1.1.0`, partition chuyển slot.
- Log sau reboot: `Police effect: enabled`.
- Node online trở lại; app có thêm Police trong Effect, chọn Police và quan sát
  đỏ–xanh luân phiên trên đèn. Bật/tắt, đổi độ sáng và quay về các chế độ cũ vẫn hoạt động.

Sau reboot, firmware gửi cấu hình node có danh sách hiệu ứng mới. Nếu app còn hiển thị
bốn lựa chọn cũ, thử tải lại danh sách thiết bị, thoát/mở lại màn hình đèn hoặc mở lại app;
kiểm tra node online và phiên bản trên Dashboard hoặc Monitor trước khi xử lý tiếp.
Không factory reset chỉ để làm mới giao diện. Việc hiển thị và điều khiển từ app cần
được xác nhận thực tế trên từng máy/board của nhóm.

### Kịch bản trình diễn tính năng mới

1. Cho xem log A `1.0.0` / `Police effect: disabled`, node online và đèn điều khiển được.
2. Trên app, mở Effect: chỉ có bốn hiệu ứng cũ; chụp màn hình làm đối chứng.
3. Build B với flag ON, upload và tạo OTA task theo mục 4–5. Chỉ A được flash bằng USB.
4. Theo dõi OTA task, reboot, log B `1.1.0` / `Police effect: enabled` và node online lại.
5. Tải lại màn hình đèn nếu cần, chọn Police, cho xem đèn đổi đỏ–xanh.
6. Chỉnh Brightness, tắt/bật Power, rồi về Static để chứng minh các điều khiển vẫn hoạt động.

> Bản 1.1.0 bổ sung hiệu ứng Police đỏ–xanh, bên cạnh bốn hiệu ứng cũ.
> Em gửi bản này qua RainMaker Dashboard. Sau cập nhật, thiết bị chạy bản mới và
> app có thể điều khiển hiệu ứng vừa bổ sung, không cần flash B bằng USB.

### Kiểm thử hồi quy trên đèn

Bảng dưới mô tả các kết quả **mong đợi**. Người chạy cần ghi pass/fail cho từng mục;
không coi toàn bộ bảng đã đạt chỉ từ việc kiểm tra Police thành công:

| Kiểm tra | Kết quả mong đợi |
|---|---|
| A: mở Effect | Chỉ có Static, Blink, Breathing, Rainbow |
| B: mở Effect sau OTA | Có thêm Police |
| B: chọn Police khi Power ON, Brightness > 0 | Toàn dải đổi đỏ–xanh mỗi 250 ms |
| B: đổi Brightness trong Police | Cường độ sáng thay đổi, vẫn giữ hai màu; 0 thì tối |
| B: Power OFF rồi ON trong Police | Tắt hẳn; bật lại thì tiếp tục Police |
| B: chọn Police khi Power OFF | Đèn vẫn tắt; chỉ chạy khi Power ON |
| B: Police → Static | Dừng Police và hiển thị màu HSV được chọn |
| B: Police ↔ Blink/Breathing/Rainbow | Hiệu ứng vừa chọn hoạt động, không còn Police chạy chồng |
| A/B: chỉnh màu, sáng ở Static; thử bốn hiệu ứng cũ | Các chức năng cũ vẫn dùng được |
| A/B: bấm Boot một lần | Đảo Power và đồng bộ app; không nhấn giữ vì đó là factory reset |

Nếu firmware bị từ chối, kiểm tra project name/phiên bản/chip. Nếu lỗi HTTPS, kiểm tra
Internet, thời gian và certificate bundle. Nếu không nhận task, kiểm tra node online,
quyền admin, đã flash A có topics OTA hay chưa và tùy chọn Force Push.
Nếu B không có Police, kiểm tra log `Police effect: enabled`, đúng file vừa build,
flag ON và làm mới cấu hình trên app. Nếu A đã có Police, đã build nhầm ON;
build/flash lại A với OFF trước khi quay demo. Bấm Build trong VS Code có thể giữ
flag từ cache của lần trước; dùng lệnh đầy đủ ở mục 3/4 để phân biệt chính xác hai bản.

### Lỗi đã gặp: `rejected — Same version received`

Thông báo này nghĩa **version bên trong image nhận được trùng version đang chạy**.
Có hai trường hợp cần phân biệt:

| Kiểm tra | Nguyên nhân/cách xử lý |
|---|---|
| Dashboard Nodes đã là `1.1.0`, Police chạy được | B có thể đã được cập nhật trước đó; gửi lại cùng B bị từ chối là đúng. Xem job trước và tính năng trên app |
| Node vẫn `1.0.0`, image trên Dashboard mang nhãn `1.1.0` | Có thể upload nhầm binary A rồi nhập override `1.1.0`. Dùng `esptool image-info` ở mục 4 để kiểm tra file thực; build lại B với ON, upload **image mới**, tạo task mới |

Trong lần thực hành, log job cho biết file 1706592 bytes; kiểm tra binary trên máy cho
thấy `App version: 1.0.0`, Police OFF và MD5 khớp file được gửi. Vì vậy nhãn `1.1.0`
trên Dashboard lúc đó không phản ánh phiên bản thực của binary. Cách sửa là build
đúng B rồi upload lại file mới, không đổi nhãn image cũ hoặc tắt kiểm tra phiên bản.

### Demo lại từ đầu

Thoát Monitor bằng **Ctrl+]**, build A với `PROJECT_VER=1.0.0` và Police OFF, flash đầy đủ
theo mục 3 rồi xác nhận node chạy A. Không cần tạo lại tài khoản hoặc factory reset nếu
thông tin provisioning còn dùng được. Build B với ON theo mục 4, tạo image/task mới
theo mục 5. Sau khi build B, không bấm Flash vì file ở `build/5_rainmaker.bin` đã là B.

Demo này xác nhận OTA thành công; không coi là đã kiểm thử cơ chế rollback chỉ vì
thư viện có hỗ trợ. Không gửi khóa/cert claiming, Wi-Fi password hay token lên GitHub.

## 7. Đưa thay đổi lên GitHub

Làm trên nhánh `feat/ota-upgrade`. Sau khi demo thực tế đạt, kiểm tra:

```powershell
git status
git diff --check
git diff
```

Chỉ stage mã nguồn/tài liệu cần thiết của hai project, commit rồi push nhánh và tạo
Pull Request vào `main`. Không stage `build/`, `sdkconfig` cá nhân hoặc `.local/`.
CI hiện tại chỉ build `1_blink`, nên dấu xanh chưa thay thế build hai project OTA.
