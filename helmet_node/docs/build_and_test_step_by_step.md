# Hướng dẫn build và kiểm thử Smart Helmet / Bike Node

Ngày đối chiếu source: 2026-10-14. Baseline firmware: `9ebe08b`.

Tài liệu dành cho Windows PowerShell, ESP-IDF 5.5.4, Helmet ESP32-S3 và
Bike ESP32-C3 SuperMini. Các lệnh chạy trên máy đang chứa repository tại
`D:\KLTN_ESP32\smart_helmet_iot`.

> Đây là quy trình thực hiện, không phải báo cáo hardware PASS.
> Thực hiện theo thứ tự; bài FAIL phải xử lý trước khi tích hợp phần liên quan.
> Không nối relay vào ECU, mạch đề hoặc động cơ xe thật.

## 1. Phạm vi và điều kiện nghiệm thu

| Mức kiểm tra | Chứng minh được | Không chứng minh được |
|---|---|---|
| Host mock | Logic chạy với dữ liệu giả lập | GPIO, radio, cảm biến, flash vật lý |
| Build | Source biên dịch và liên kết được | Firmware hoạt động trên board |
| Flash | Ghi firmware vào đúng board | Cảm biến hoặc toàn hệ thống hoạt động đúng |
| Hardware module | Module phản ứng đúng thao tác và số đo | Toàn bộ tích hợp |
| Integration | Các module phối hợp trong tình huống đã thử | Độ bền dài hạn hoặc an toàn triển khai trên xe |
| Soak | Ổn định trong thời gian thử đã ghi | Bảo đảm hoạt động vô thời hạn |

Các giới hạn hiện tại:

- RC522 vẫn là scaffold; hàm kiểm tra UID luôn trả false, chưa có UID được cấp quyền.
- OLED Bike chưa có driver hoàn chỉnh; cần xác nhận controller và kích thước.
- BLE hiện chưa xác thực/bond với một Helmet đã đăng ký; service UUID không phải cơ chế chống giả mạo.
- Relay hiện bám theo quyền xác thực và bị đưa về LOW khi mất quyền. Chưa có cơ chế được kiểm chứng để bảo đảm không ngắt động cơ đang chạy.
- Bike hardware-test runner riêng chỉ khởi tạo trạng thái relay an toàn và in cấu hình; không phải bài test RC522/OLED/BLE đầy đủ.
- Chưa có kết quả board thật cho bản tích hợp này. Không đánh dấu toàn hệ thống PASS khi các mục trên còn BLOCKED.

## 2. Chuẩn bị và kiểm tra dây

Chuẩn bị hai board, hai cáp USB truyền dữ liệu, đồng hồ đo, nguồn phù hợp,
máy Windows có GCC host và ESP-IDF, hotspot Wi-Fi 2.4 GHz, endpoint thử nghiệm
do bạn kiểm soát cho các bài cloud. Chỉ dùng LED có điện trở/tải mô phỏng cho relay.

1. Tắt nguồn trước khi đổi dây; kiểm tra không chập nguồn/GND.
2. Xác minh nguồn cấp từng module theo đúng model; không suy ra VCC từ mức tín hiệu UART/I2C.
3. Không đưa tín hiệu 5 V vào GPIO ESP32. MQ-3 AO phải qua mạch chia áp đã được kiểm tra trước khi nối GPIO4.
4. Mạch MQ-3 có bộ nung: không đổ cồn hoặc đưa chất lỏng/dung môi dễ cháy vào cảm biến. Bài hiệu chuẩn cần quy trình và mẫu chuẩn riêng.
5. Relay bạn mô tả là 5 V nhưng VCC nối 3.3 V. Cần ảnh/model/datasheet để xác nhận nguồn và ngưỡng IN. COM là tiếp điểm tải, không phải nguồn nuôi cuộn relay.
6. Khi chưa xác minh relay, tháo module khỏi GPIO1 và chỉ đo tín hiệu GPIO1 so với GND. Không tự đổi VCC sang 5 V nếu chưa kiểm tra ngõ IN tương thích ESP32.

### 2.1. Helmet: đối chiếu `main/pin_config.h`

| Chức năng | GPIO / yêu cầu |
|---|---|
| MPU6050 SDA / SCL / INT | 8 / 9 / 7 |
| GPS RX / TX phía ESP32 | 17 / 18; GPS TX nối RX17, GPS RX nối TX18 nếu sử dụng |
| MQ-3 ADC / power enable | 4 / 5; ADC đo tại nút sau chia áp |
| SOS | 6, nhấn xuống GND, internal pull-up, debounce 40 ms |
| LED trạng thái | 2, HIGH sáng |
| Buzzer active | 10, HIGH bật transistor NPN; LOW tắt |

### 2.2. Bike: đối chiếu `main/bike_config.h`

| Chức năng | GPIO / yêu cầu |
|---|---|
| Relay IN | 1, HIGH kích, LOW không kích |
| RC522 CS / SCK / MISO / MOSI / RST | 7 / 4 / 5 / 6 / 10; RC522 dùng 3.3 V |
| OLED SDA / SCL / địa chỉ | 9 / 8 / 0x3C; controller/kích thước chưa xác nhận |

Không thay pin, WHO_AM_I, ngưỡng hoặc active level để che lỗi wiring.

## 3. Mở PowerShell và nhận diện thiết bị

### 3.1. Kiểm tra bản source đang dùng

Trong PowerShell thường:

```powershell
cd D:\KLTN_ESP32\smart_helmet_iot
git -c safe.directory=D:/KLTN_ESP32/smart_helmet_iot status --short --branch
git -c safe.directory=D:/KLTN_ESP32/smart_helmet_iot rev-parse HEAD
```

Ghi SHA vào phiếu test. Nếu source có sửa đổi, ghi cả tên file và giá trị macro đã đổi.
PR chưa merge thì clone nhánh main từ remote có thể chưa có bản này; xác nhận có
`helmet_node`, `bike_node`, `shared` và bài host test trước khi tiếp tục.
Không reset hoặc checkout đè các thay đổi chưa lưu.

### 3.2. Mở shell ESP-IDF

Chạy từ PowerShell thường:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -NoExit -Command "& 'C:\Espressif\tools\Microsoft.v5.5.4.PowerShell_profile.ps1'"
```

Các lệnh `idf.py` bên dưới chạy trong shell vừa mở:

```powershell
idf.py --version
python --version
```

Mong đợi ESP-IDF v5.5.4. Nếu profile không tồn tại hoặc `idf.py` không được nhận diện,
mở ESP-IDF terminal từ Espressif Installation Manager của bản đã cài; không cài chồng
toolchain tùy ý. Nếu máy chưa có ESP-IDF, bước build là BLOCKED cho đến khi cài đúng môi trường.

### 3.3. Xác định cổng COM, không đoán

Rút hai board, chạy lệnh sau; cắm từng board rồi chạy lại:

```powershell
Get-CimInstance Win32_SerialPort | Select-Object DeviceID, Name
```

Đối chiếu thêm Device Manager nếu cổng USB không xuất hiện trong kết quả.
Ghi nhãn cáp Helmet/Bike. Cổng có thể đổi khi vào bootloader.
Đóng Serial Monitor khác trước khi flash; mỗi COM chỉ một chương trình sử dụng.

## 4. Test logic trên máy tính trước

Chạy từ root (không cần cắm board):

```powershell
cd D:\KLTN_ESP32\smart_helmet_iot
gcc --version
powershell.exe -NoProfile -ExecutionPolicy Bypass -File helmet_node/hardware_tests/host/run.ps1
```

Mong đợi:

```text
PASS: 85 safety checks (host mocks, not hardware)
PASS: 75 cloud checks (mock HTTP/NVS, no network)
```

Runner dùng GCC C11 với `-Wall -Wextra -Werror`. Cần GCC host trong PATH, không phải
compiler Xtensa/RISC-V của board. Test cloud dùng cJSON trong `$env:IDF_PATH`, hoặc
đường dẫn dự phòng `D:/esp/v5.5.4/esp-idf`. Không tìm thấy GCC/cJSON là lỗi môi trường,
không được coi là test PASS. Tổng 160 checks là baseline, không phải số đo phần cứng.

## 5. Build hai project độc lập

Không chạy `idf.py build` ở root repository. Mỗi node có CMake, sdkconfig và build riêng.

### 5.1. Helmet ESP32-S3

```powershell
cd D:\KLTN_ESP32\smart_helmet_iot\helmet_node
idf.py reconfigure
Select-String -Path sdkconfig -Pattern '^CONFIG_IDF_TARGET=', '^CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE='
idf.py menuconfig
```

Trong menuconfig kiểm tra, không ghi mật khẩu vào tài liệu/log chia sẻ:

| Mục | Giá trị cho ứng dụng tích hợp |
|---|---|
| Target | `esp32s3` |
| Partition Table | Single factory app (large), `CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y` |
| Bluetooth | `CONFIG_BT_ENABLED=y`, `CONFIG_BT_NIMBLE_ENABLED=y` |
| Smart Helmet Phase 7 Communication: BLE | Bật |
| Cloud | Bật khi test cloud; endpoint thật của môi trường thử nghiệm |
| Wi-Fi | Tắt cho bài offline; bật và nhập SSID/password cho bài cloud |
| Helmet IO: SOS / LED / buzzer active level | 0 / 1 / 1 |
| Helmet IO: buzzer frequency | 0, vì buzzer active |

`sdkconfig.defaults` không tự ghi đè mọi giá trị trong sdkconfig cũ. Sau khi lưu menuconfig:

```powershell
idf.py build
if ($LASTEXITCODE -ne 0) { throw 'Helmet build failed; do not flash' }
idf.py size
Get-FileHash .\build\smart_helmet_iot.bin -Algorithm SHA256
```

Nếu báo target sai, dừng lại và kiểm tra đúng thư mục. Chỉ khi chắc chắn cần đổi target,
sao lưu cấu hình riêng rồi chạy `idf.py set-target esp32s3`; thao tác này tái tạo cấu hình,
cần kiểm tra lại menuconfig. Không chạy set-target mỗi lần build.

### 5.2. Bike ESP32-C3

Mở shell ESP-IDF thứ hai nếu cần monitor cả hai board cùng lúc:

```powershell
cd D:\KLTN_ESP32\smart_helmet_iot\bike_node
idf.py reconfigure
Select-String -Path sdkconfig -Pattern '^CONFIG_IDF_TARGET='
idf.py menuconfig
```

Target phải là `esp32c3`; bật `CONFIG_BT_ENABLED` và `CONFIG_BT_NIMBLE_ENABLED`.
Nếu thật sự sai target, áp dụng quy trình sao lưu/kiểm tra như Helmet rồi dùng
`idf.py set-target esp32c3`. Sau khi lưu:

```powershell
idf.py build
if ($LASTEXITCODE -ne 0) { throw 'Bike build failed; do not flash' }
idf.py size
Get-FileHash .\build\bike_node.bin -Algorithm SHA256
```

### 5.3. Quality gate build

- Phải có `Project build complete`, không có compile/link/partition overflow error.
- Đọc và ghi nhận warning; không bỏ qua chỉ vì exit code của shell ngoài là 0.
- Giữ log build, kích thước app và SHA256 binary cho mỗi cấu hình đã thử.
- Khi endpoint HTTPS được cấu hình, phải build lại và kiểm tra kích thước; bản URL rỗng không thay thế kiểm tra cấu hình HTTPS.
- Không dùng `erase-flash` để chữa lỗi build; build chưa tác động đến board.
- Không đưa `sdkconfig`, mật khẩu, token hoặc log chứa thông tin nhạy cảm lên Git.

## 6. Flash và lưu log chuẩn

Chạy trong đúng thư mục node, trong shell ESP-IDF. Nhập đúng COM đã xác định:

```powershell
$port = Read-Host 'Nhap COM cua node dang test, vi du COM11'
idf.py -p $port flash
if ($LASTEXITCODE -ne 0) { throw 'Flash failed; stop and check port/board' }
idf.py -p $port monitor
```

1. Kiểm tra log boot đúng tên node và đúng chế độ/phase.
2. Trong monitor, nhấn `Ctrl+T`, thả, rồi `Ctrl+L` để bật lưu log; monitor báo file được tạo trong thư mục project.
3. Sau khi bật lưu log, reset bằng nút RESET/EN để lưu trọn log boot. Việc mở monitor cũng có thể reset board.
4. Ghi thời điểm thao tác: nhấn SOS, tắt Wi-Fi, reset Helmet, bắt đầu/kết thúc bài.
5. Thoát monitor bằng `Ctrl+]`; không dùng Ctrl+C làm thao tác thoát mặc định.
6. Chỉ mở lại monitor khi đúng file ELF/build tương ứng với firmware đang chạy để chẩn đoán lỗi chính xác.

Lần flash bản này phải dùng `flash` đầy đủ vì partition Helmet đã đổi sang app lớn.
Không dùng `app-flash` cho lần cập nhật partition đó. Không xóa NVS trong bài test
lưu báo cáo qua reset. Nếu cần recovery bằng erase thì ghi rõ mất dữ liệu và bắt đầu
lại bài lưu trữ, không tính lần đó là persistence PASS.

## 7. Test Helmet theo từng phase độc lập

Mở `helmet_node/main/system_config.h`, chỉ thay các macro chọn bài:

```c
#define HARDWARE_TEST_MODE 1
#define HARDWARE_TEST_PHASE 1
```

Giữ `USE_MOCK_SENSOR_DATA=0`. Với mỗi phase 1 đến 6:

1. Thoát monitor.
2. Đổi `HARDWARE_TEST_PHASE` sang số cần thử, lưu file.
3. Từ `helmet_node`, chạy `idf.py build`; dừng nếu lỗi.
4. Flash và monitor theo mục 6; ghi SHA256 binary mới.
5. Chỉ ghi PASS sau khi có bằng chứng ứng với tiêu chí bên dưới.

### H01. Phase 1: Board

- Để board chạy ít nhất 5 phút rồi reset 3 lần.
- Mong đợi `ESP32-S3 bring-up test`, thông tin flash/heap và `PASS heartbeat` liên tục.
- PASS: boot ổn định, không brownout/panic/watchdog hoặc reboot không chủ đích.
- Đây chưa phải kiểm tra LED, buzzer, SOS hay cảm biến.

### H02. Phase 2: MPU6050

- Dây SDA8, SCL9, INT7; nguồn và GND đúng theo module.
- Mong đợi ACK tại địa chỉ phù hợp, `WHO_AM_I=0x68`, cấu hình readback đúng, DATA_READY liên tục.
- Đặt yên: tổng gia tốc gần 1 g; xoay nhẹ từng trục: số đo thay đổi hợp lý.
- PASS: cả giao tiếp, ngắt và phản ứng vật lý đều có bằng chứng.
- No ACK/timeout/no DATA_READY: FAIL; kiểm tra dây, nguồn, pull-up, INT. Không sửa WHO_AM_I để cho qua.

### H03. Phase 3: GPS

- GPS TX nối ESP32 RX17; baud bài test là 9600. Đặt anten ở vị trí thoáng trời.
- Mong đợi `GPS PARSER PREFLIGHT` không fail, sau đó `NMEA:` và `GGA parsed:`.
- Chờ fix; ghi thời gian chờ, sats, hdop, lat/lon và đối chiếu vị trí thực.
- PASS giao tiếp: NMEA hợp lệ liên tục. PASS định vị: có fix thật và tọa độ hợp lý.
- Chưa fix trong điều kiện quan sát: ghi NOT VERIFIED/BLOCKED kèm thời gian, không suy ra GPS hỏng chỉ vì parser chưa có fix.
- Không chia sẻ log tọa độ cá nhân khi chưa ẩn dữ liệu vị trí.

### H04. Phase 4: MQ-3

- Trước khi nối ADC, kiểm tra mạch chia áp; đo tại nút sẽ nối GPIO4, không dùng trực tiếp AO có thể lên 5 V.
- Chờ firmware warm-up 60 giây. Đây là thời gian phần mềm hiện đặt, không phải xác nhận cảm biến đã ổn định/hiệu chuẩn theo datasheet.
- Mong đợi `Warm-up complete`, sau đó `SAMPLE: raw=... ADC-node average=... alcohol=...`.
- So sánh điện áp log với đồng hồ tại nút ADC; ghi sai lệch thực tế và kiểm tra không bão hòa.
- Ngưỡng code hiện là 1.80 V tại ADC-node, không phải BAC hay nồng độ cồn đã hiệu chuẩn.
- PASS đọc ADC và PASS phân loại/hiệu chuẩn là hai kết quả riêng. Chưa có mẫu chuẩn và dung sai được duyệt thì phần hiệu chuẩn là BLOCKED.
- Không uống rượu để thử, không đưa dung môi dễ cháy gần bộ nung. Nhánh logic PASS/FAIL đã có mock; bài vật lý cần quy trình mẫu chuẩn riêng.

### H05. Phase 5: Accident detector

- Xem preflight upright/impact/tilt/reset: phải PASS; phần này dùng mẫu tổng hợp.
- Đặt cụm cảm biến trên gá an toàn, nghiêng hơn 60 độ và giữ hơn 2 giây; quan sát `PASS FALL`.
- Trả về vị trí bình thường, kiểm tra dữ liệu tiếp tục cập nhật.
- Không đập/thả mũ hoặc tạo tai nạn để thử ngưỡng impact 3 g.
- PASS FALL vật lý không đồng nghĩa PASS IMPACT vật lý. Nhánh impact vật lý cần thiết bị/quy trình thử được phê duyệt; chưa có thì BLOCKED.

### H06. Phase 6: FSM

- Bài tự gửi event qua queue, kiểm tra các chuyển trạng thái.
- Mong đợi `FSM TEST COMPLETE: pass=... fail=0`.
- PASS này chứng minh FSM/queue trên board, không chứng minh SOS vật lý hoặc radio.

## 8. Chuyển về ứng dụng tích hợp Helmet

Đổi `helmet_node/main/system_config.h`:

```c
#define HARDWARE_TEST_MODE 0
```

Đảm bảo macro `USE_MOCK_SENSOR_DATA` hiện có là 0, không thêm một định nghĩa trùng.
Build, flash đầy đủ, lưu log mới. Phải thấy `Sensor mock mode: 0`.
Giữ Helmet cố định, tránh nghiêng vô tình kích hoạt detector trong bài khác.

### I01. Khởi động offline

1. Tắt hotspot; để các cảm biến đã PASS kết nối đầy đủ.
2. Reset Helmet, không nhấn SOS.
3. Mong đợi `MQ-3 warm-up`, sau khoảng 60 giây có `raw_avg`, `voltage_avg`, `result`, `sensor_ok`.
4. Kết quả PASS hợp lệ: STARTUP_ALCOHOL_CHECK -> READY_TO_RIDE -> DRIVING_MONITORING.
5. Kết quả FAIL: LOW_POWER_IDLE; không tự cấp quyền. Khi muốn chạy lại kiểm tra khởi động, reset board.
6. Lỗi khởi tạo cảm biến phải được ghi nhận riêng; không coi hệ thống đầy đủ chỉ vì SOS còn hoạt động.

### I02. SOS, LED và buzzer

1. Reset; trong lúc MQ-3 đang warm-up, nhấn SOS hơn 100 ms.
2. Mong đợi `System event: SOS_PRESSED` và EMERGENCY_REPORTING ngay, không chờ warm-up kết thúc.
3. Giữ 5 giây: đúng một event. Thả ít nhất 100 ms, nhấn lại: thêm một event.
4. Lặp 10 lần có nhấn/thả rõ ràng; ghi số lần nhấn và số event.
5. Buzzer GPIO10 LOW lúc khởi tạo; emergency khoảng 300 ms bật/300 ms tắt. Không kêu liên tục ngoài mẫu yêu cầu.
6. LED đổi mẫu theo trạng thái; LED emergency nhanh hơn monitoring. READY có thể tồn tại rất ngắn nên không dùng quan sát mắt để kết luận thiếu trạng thái.
7. Khi MQ-3 trả kết quả sau SOS, còi khẩn cấp không bị thay bằng beep ngắn.
8. Reset giữa các kịch bản độc lập. Hiện chưa có thao tác nút để xác nhận/thoát emergency trong ứng dụng.

### I03. GPS trong ứng dụng và dữ liệu stale

Ứng dụng không in toàn bộ GPS liên tục. Có thể dùng payload cloud để kiểm tra snapshot:

1. Hoàn thành cấu hình cloud ở mục 10, chờ GPS có fix, nhấn SOS và xem payload server.
2. Dùng công tắc ngắt riêng đường TX GPS đã bố trí khi tắt nguồn, không rút/chạm dây nguồn tùy tiện khi đang chạy.
3. Ngắt đường dữ liệu, chờ hơn 5 giây rồi nhấn-thả SOS để tạo báo cáo mới.
4. Mong đợi `gps.valid=false`, tuổi dữ liệu trên 5000 ms; tọa độ cũ không được đánh dấu hợp lệ.
5. Nối lại đường dữ liệu, chờ fix mới; tạo báo cáo mới để xác nhận phục hồi.

`gps.age_ms` được chụp lúc tạo báo cáo, không phải lúc server nhận sau retry.
Giá trị 4294967295 nghĩa là chưa có snapshot. Chưa có công cụ quan sát snapshot/server
thì ghi BLOCKED cho bài stale; không dùng riêng Phase 3 parser để kết luận bài này PASS.

## 9. Bike, BLE và relay an toàn

Giữ `BIKE_HARDWARE_TEST_MODE=0` trong `bike_node/main/bike_config.h`.
Build và flash Bike theo mục 5-6. Monitor cả hai node ở hai cửa sổ.

### B01. Boot và khóa mặc định

- Đo GPIO1 với tải relay tháo rời nếu module chưa được xác minh.
- Mong đợi mức LOW sau khởi tạo, heartbeat Bike khoảng 5 giây/lần, `ignition_allowed=0`.
- Kiểm tra bật nguồn/reset nhiều lần. Đồng hồ chỉ chứng minh mức tĩnh; muốn chứng minh không có xung kích ngắn khi boot cần oscilloscope/logic analyzer.
- Không gắn relay thật để nghiệm thu nếu chưa biết nguồn/IN tương thích; ghi BLOCKED phần module relay.

### B02. Kết nối và reconnect

1. Bật Helmet ở ứng dụng thật, bật Bike.
2. Bike phải log `Helmet connected; discovering service`, sau đó `Helmet notifications subscribed`.
3. Reset Helmet, đợi nối lại; lặp 10 lần. Ghi thời gian nối lại từng lần.
4. Tắt Helmet ít nhất 10 giây, sau đó bật; Bike phải scan/reconnect không cần reset Bike.
5. Kiểm tra thêm thứ tự bật Bike trước Helmet và Helmet trước Bike.
6. PASS khi thực sự subscribe lại, dữ liệu được nhận và không panic/reset. Chỉ log connected chưa đủ.

Backoff Bike tăng tối đa 30 giây; không ép tiêu chí reconnect phải luôn dưới 1 giây.
BLE mất packet quá 3000 ms phải làm dữ liệu hết hạn; mất kết nối được xử lý ở vòng
ứng dụng tiếp theo. Đo độ trễ cần công cụ quan sát dữ liệu/quyền, không chỉ đồng hồ đo relay.

### B03. Giới hạn test authorization hiện tại

Quyền mở relay yêu cầu đồng thời RFID được phép, Helmet connected/fresh, Helmet ready
và alcohol pass. UID chưa được cấp quyền nên relay luôn tắt là hành vi hiện tại.

- BLE kết nối tốt mà relay vẫn tắt không phải lỗi tự thân.
- Quan sát relay LOW khi BLE mất, trong khi RFID vốn false, không chứng minh nhánh thu hồi một quyền đang bật.
- Không sửa allowlist thành luôn true hoặc fake UID để báo integration PASS.
- Test thẻ hợp lệ/không hợp lệ, chống lặp UID, relay ON/OFF đầy đủ và OLED đều BLOCKED đến khi triển khai module, UID và thông tin màn hình.
- Packet sai độ dài/version/flags, trùng/lùi sequence hiện được kiểm tra ở host mock. Test qua radio thật cần peripheral/harness có khả năng phát packet lỗi; chưa có harness thì BLOCKED phần radio fault injection.
- Không dùng firmware này để cắt động cơ khi mất BLE; thiết kế mạch chỉ cho phép khởi động và chính sách trạng thái chạy cần được duyệt trước khi triển khai xe.

## 10. Wi-Fi, cloud và lưu báo cáo qua reset

### 10.1. Chuẩn bị endpoint trước khi chạy

Cần endpoint do bạn kiểm soát có thể nhận HTTP POST JSON, lưu request và chọn trả
200/204/400/500 hoặc cố ý trì hoãn quá timeout. Chưa có endpoint thì ghi BLOCKED,
không dùng URL `example.invalid` trong firmware thử thực tế.

- Điện thoại/hotspot phải cho Helmet truy cập server. `localhost` trên Helmet không phải máy tính của bạn.
- Với server trên PC, dùng IP LAN của PC, bind đúng interface và chỉ mở firewall phạm vi mạng thử được phép; không tắt toàn bộ firewall.
- HTTPS cần chứng chỉ hợp lệ được firmware tin cậy. Không tắt xác minh TLS chỉ để có PASS.
- Token và tọa độ là dữ liệu riêng; không gửi đến dịch vụ công cộng không được phép. HTTP không mã hóa chỉ phù hợp mạng thử cô lập, không mang token thật.

Từ `helmet_node`, chạy `idf.py menuconfig`, cấu hình menu
`Smart Helmet Phase 7 Communication`:

| Tùy chọn | Giá trị thử |
|---|---|
| Enable Wi-Fi station manager | Bật |
| Wi-Fi SSID/password | Hotspot thử nghiệm |
| Enable emergency HTTP cloud client | Bật |
| Emergency HTTP/HTTPS endpoint URL | URL nhận POST của bạn |
| Bearer token | Theo server, không ghi vào báo cáo |
| Queue length | 4 để khớp bài dưới |
| Timeout | 5000 ms để khớp bài dưới |
| Device ID | Giá trị nhận diện board thử |

Build lại, kiểm tra partition/size, flash đầy đủ. Endpoint rỗng có log
`BLOCKED: endpoint empty; retaining reports`, không phải cloud PASS.

### C01. Gửi thành công

1. Server trả 200 hoặc 204; bật hotspot, đợi có IP.
2. Nhấn-thả SOS, chờ server nhận POST.
3. Kiểm tra JSON gồm `device`, `report_id`, `event`, `state`, `uptime_ms`, `gps`.
4. `event` phải phù hợp thao tác, GPS có/không fix đúng thực tế.
5. Mong đợi `Emergency report delivered, status=...`; server thực sự có request tương ứng.

### C02. Lỗi HTTP và timeout

1. Để outbox của bài trước gửi hết; đổi server sang 400, nhấn SOS.
2. Quan sát `HTTP delivery failed` và `Report retained; retry in ... ms`.
3. Chuyển server về 204: bản tin được gửi lại và xác nhận delivered.
4. Lặp riêng với 500, 301 và response trễ hơn 5 giây.
5. PASS: chỉ 2xx được xóa, lỗi giữ lại; backoff 1, 2, 4, 8, 16, 32, tối đa 60 giây. Khoảng cách request thực còn cộng thời gian HTTP timeout.

Không trộn nhiều bài khi queue còn bản tin cũ mà không ghi report_id, vì sẽ khó xác định request nào đang retry.

### C03. Mất Wi-Fi và reset khi offline

1. Tắt hotspot trong khi board đang chạy; nhấn SOS.
2. Nếu thấy `Emergency awaiting durable outbox acceptance` hoặc `Outbox persistence failed`, chưa được coi là bản tin bền trong NVS.
3. Chờ vài giây để worker/application có cơ hội commit; firmware hiện không log xác nhận commit thành công riêng.
4. Reset Helmet khi vẫn offline, bật lại hotspot, không nhấn SOS mới.
5. Server phải nhận báo cáo của trước reset. Đây là bằng chứng phục hồi thực tế; chỉ chờ vài giây không tự nó chứng minh commit.
6. Lặp 3 lần. Tắt hotspot ít nhất 3 phút trong một lần để kiểm tra reconnect khi backoff đã tăng.

Không xóa NVS. Reset sau HTTP ACK nhưng trước commit xóa có thể gửi trùng; server
cần deduplicate theo `device` + `report_id`. Không tuyên bố exactly-once.
Reset có kiểm soát không thay thế kiểm chứng mất điện đúng thời điểm flash đang ghi.

### C04. Queue đầy

1. Queue length=4, để các báo cáo trước gửi hết rồi tắt hotspot.
2. Nhấn-thả SOS 6 lần, cách nhau ít nhất 3 giây; xác nhận đủ 6 event trong log.
3. Mong đợi `Outbox full: replace oldest with newest emergency` khi vượt dung lượng.
4. Reset khi offline rồi bật hotspot; đối chiếu báo cáo đã giữ với log/thời điểm trước reset.
5. PASS theo chính sách giữ tối đa 4 báo cáo mới nhất đã được nhận vào outbox; không kỳ vọng lưu vô hạn.

NVS full/corrupt/commit failure vật lý cần môi trường fault-injection riêng.
Host mock đã có các trường hợp lỗi, nhưng không thay thế kiểm chứng flash thật.
Không cố tình làm hỏng NVS hoặc cắt nguồn lúc ghi trên bản thiết bị đang cần giữ dữ liệu.

## 11. Chạy bền tối thiểu 30 phút

Chỉ bắt đầu sau khi các bài độc lập liên quan đã PASS. Dùng firmware ứng dụng thật,
hai monitor và server thử; không dùng hardware-test phase riêng cho bài này.

| Khoảng thời gian | Thao tác |
|---|---|
| 0-5 phút | Boot hai node, theo dõi warm-up, BLE subscribe, heap và trạng thái |
| 5-10 phút | SOS 3 lần có nhấn/thả, kiểm tra server và LED/buzzer |
| 10-15 phút | Tắt rồi bật hotspot, kiểm tra lưu/gửi lại |
| 15-20 phút | Reset Helmet 3 lần, theo dõi Bike reconnect |
| 20-30 phút | Để chạy ổn định, SOS thêm một lần, kiểm tra báo cáo cuối |

Ghi log Helmet `health uptime=... heap=... min_heap=... stack=...` mỗi 60 giây,
heartbeat Bike khoảng 5 giây và timestamp thao tác. Uptime Helmet giảm khi chủ động
reset là bình thường; phải phân biệt với reset không chủ đích.

PASS: không panic/watchdog/brownout/reset ngoài kế hoạch; thao tác vẫn được xử lý,
BLE/Wi-Fi phục hồi, không tăng dồn task/tài nguyên hoặc giảm free heap liên tục trong
điều kiện tải tương đương. Minimum heap chỉ giảm hoặc giữ nguyên theo định nghĩa,
không dùng riêng chỉ số đó để kết luận memory leak. Ghi stack watermark nhỏ nhất,
không tự gọi là đủ stack nếu chưa có biên an toàn đo được theo workload.

30 phút là mốc kiểm tra ban đầu; muốn nghiệm thu dài hạn cần thêm chu kỳ nhiều giờ/ngày
và workload đã thống nhất. Các module BLOCKED vẫn phải giữ trạng thái BLOCKED sau soak.

## 12. Xử lý lỗi thường gặp

| Hiện tượng | Thao tác tiếp theo |
|---|---|
| `idf.py` không tìm thấy | Mở lại đúng shell ESP-IDF, kiểm tra profile và version |
| `gcc` không tìm thấy | Cấu hình GCC host trong PATH; không dùng compiler board cho host runner |
| COM bị chiếm | Thoát monitor/công cụ serial khác, xác định lại COM |
| Không kết nối flash | Kiểm tra cáp data/nguồn/COM; dùng quy trình BOOT/RESET đúng board, không đổi target để thử mò |
| Target không khớp chip | Dừng flash, kiểm tra project Helmet/S3 hoặc Bike/C3 |
| App quá lớn | Kiểm tra partition Helmet large và cấu hình HTTPS, không bỏ tính năng xác minh TLS để giảm size |
| MPU no ACK | Kiểm tra nguồn/GND/SDA/SCL, địa chỉ/INT; không kết luận PASS từ idle HIGH |
| GPS có NMEA nhưng không fix | Kiểm tra anten/vị trí/thời gian chờ; tách giao tiếp khỏi định vị |
| MQ-3 luôn min/max | Kiểm tra chia áp, power enable, nguồn và ADC-node bằng đồng hồ |
| BLE không subscribe | Kiểm tra NimBLE hai node, Helmet BLE enable, chạy ứng dụng chứ không hardware phase |
| Relay luôn LOW | Hiện tại đúng vì UID chưa được cấp quyền; không bypass xác thực |
| OLED không hiển thị | Driver chưa hoàn thiện; ghi BLOCKED, không quy lỗi phần cứng từ firmware này |
| Cloud không gửi | Kiểm tra enable, endpoint, IP, quyền mạng, TLS và log retry; không in token ra log |
| Queue không phục hồi | Giữ log trước/sau reset và report_id; không erase NVS trước khi điều tra |

## 13. Phiếu kết quả để điền

Không điền PASS sẵn. Mỗi cấu hình phase tạo binary khác nhau, cần ghi đúng SHA256.

```text
Ngay/gio:
Nguoi test:
Git commit:
Source da sua / macro / phase:
ESP-IDF version:
Board Helmet / COM / firmware SHA256:
Board Bike / COM / firmware SHA256:
Nguon cap / wiring / relay module:
Endpoint thu nghiem (khong token):
Test ID:
Dieu kien ban dau:
Thao tac va timestamp:
Ket qua mong doi:
Ket qua thuc te / so do:
File log / anh / bang chung server:
Ket luan: PASS / FAIL / BLOCKED / NOT RUN
Loi con lai va buoc xu ly:
```

| Hạng mục | Kết quả mới | Bằng chứng cần giữ |
|---|---|---|
| Host 160 checks | NOT RUN | Output runner |
| Build Helmet và Bike | NOT RUN | Log, size, SHA256 |
| H01-H06 | NOT RUN | Log từng phase, số đo và thao tác |
| I01-I03 | NOT RUN | Log ứng dụng, IO, GPS snapshot |
| B01-B02 | NOT RUN | GPIO, log hai board, thời gian reconnect |
| RFID/relay mở quyền/OLED | BLOCKED | Cần triển khai và thông tin phần cứng/UID |
| BLE packet lỗi qua radio/bonding | BLOCKED | Harness và cơ chế xác thực chưa hoàn chỉnh |
| C01-C04 | NOT RUN | Log thiết bị và server, report_id |
| Power-loss/NVS fault injection vật lý | BLOCKED | Cần quy trình riêng |
| Soak 30 phút | NOT RUN | Log liên tục và bảng thao tác |

Sau khi test xong: trả `HARDWARE_TEST_MODE=0`, `USE_MOCK_SENSOR_DATA=0`,
`BIKE_HARDWARE_TEST_MODE=0`; build/flash lại cấu hình ứng dụng cuối và ghi SHA256.
Kiểm tra `git diff` để không commit nhầm macro test, mật khẩu hoặc log riêng tư.
Không tự merge/push thay đổi cấu hình thử vào main.

## 14. Tài liệu/source dùng đối chiếu

- `helmet_node/main/system_config.h`, `pin_config.h`, `main.c`.
- `helmet_node/hardware_tests/phase1_board_test.c` đến `phase6_fsm_test.c`.
- `helmet_node/hardware_tests/host/run.ps1`.
- `helmet_node/components/helmet_io/`, `gps_driver/`, `cloud_client/`, `wifi_manager/`.
- `helmet_node/components/ble_comm/`, `bike_node/components/ble_comm/`.
- `bike_node/main/bike_config.h`, `bike_app.c`, `components/bike_state/`.
- `helmet_node/sdkconfig.defaults`, `bike_node/sdkconfig.defaults`.
- [Báo cáo và checklist Phase 8-11](phase_8_to_11_validation.md).
- Phím tắt monitor được đối chiếu với tài liệu ESP-IDF cài tại `D:/esp/v5.5.4/esp-idf/docs/en/api-guides/tools/idf-monitor.rst`.
