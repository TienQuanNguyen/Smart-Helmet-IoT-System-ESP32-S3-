# Kiểm chứng Phase 8–11

Không dùng kết quả build làm bằng chứng phần cứng. Chỉ thử relay với LED/tải
mô phỏng; không nối ECU hoặc mạch đề. Không tạo va đập nguy hiểm lên mũ.

## Phase 8

Các file chính: `main/main.c`, `main/app_test.c`, `main/system_config.h`,
`components/gps_driver/`, `components/helmet_io/`, `main/CMakeLists.txt`.

- Luồng ứng dụng mặc định dùng cảm biến thật; giữ mock bằng
  `USE_MOCK_SENSOR_DATA=1`. Hardware test Phase 1–6 vẫn chọn riêng như trước.
- MQ-3 warm-up 60000 ms trong task riêng, trung bình 32 mẫu, phát một kết quả.
  Lỗi đọc là FAIL. Log ADC/điện áp không phải phép đo nồng độ cồn đã hiệu chuẩn.
- GPS có một task sở hữu UART; snapshot được bảo vệ critical section, quá
  5000 ms thành invalid. Giá trị tuổi UINT32_MAX nghĩa là chưa có dữ liệu.
- Người dùng xác nhận: SOS GPIO6 xuống GND, pull-up, debounce 40 ms; LED GPIO2
  active-high; còi active GPIO10 qua NPN 8050, LOW khi khởi tạo.
- IO chạy theo tick 20 ms của FSM, không delay để phát còi hoặc nháy LED.
- Log sức khỏe mỗi 60 giây: uptime, free heap, minimum heap, stack FSM.

Lệnh từ `helmet_node/`:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "& 'C:\Espressif\tools\Microsoft.v5.5.4.PowerShell_profile.ps1'; idf.py build"
```

Log mong đợi: `MQ-3 warm-up`, sau đó `raw_avg`, `voltage_avg`, `result`;
FSM qua ALCOHOL_CHECK → READY → DRIVING khi PASS. Giữ SOS phải chỉ có một
`SOS_PRESSED`; thả và nhấn lại mới có sự kiện tiếp theo. Trong warm-up,
SOS vẫn chuyển EMERGENCY. GPS mất dữ liệu trên 5 giây phải invalid.

Build Phase 8: BUILD VERIFIED (ESP-IDF 5.5.4, binary 0xc8450, còn 22%).
Hardware: BLOCKED, chưa có flash/monitor cho bản này.

## Phase 9

File: `components/cloud_client/`, `components/wifi_manager/`, `main/main.c`.
Cloud lấy snapshot GPS tại thời điểm chấp nhận báo cáo; tuổi GPS trong payload
là tuổi tại thời điểm đó, không phải thời điểm retry. JSON được tạo bằng cJSON.
Endpoint/token chỉ lấy từ cấu hình, không in payload hoặc token ra log.

Outbox lưu một blob NVS và commit trước khi trả thành công cho ứng dụng.
Không xóa NVS nếu lỗi định dạng/dung lượng. Queue đầy: thay cảnh báo cũ nhất
bằng mới nhất, có log. HTTP 2xx mới xóa; 4xx/5xx/timeout giữ lại và retry
1, 2, 4, 8, 16, 32, 60 giây, tối đa 60 giây. Wi-Fi tự reconnect có backoff.
Reset sau HTTP thành công nhưng trước commit xóa có thể gửi trùng: server nên
deduplicate theo device/report_id. Báo cáo chưa commit không bền qua mất nguồn.
NVS chỉ ghi khi có cảnh báo hoặc ACK, không ghi mỗi vòng lặp. Cần đo độ trễ flash
trên board; HTTP nằm ở task riêng và không giữ mutex trong lúc gửi.

Kiểm thử: cấu hình cloud enable và endpoint thử nghiệm bằng menuconfig. Gây
mất mạng, 4xx/5xx/timeout phải thấy `Report retained`; 2xx phải thấy `delivered`.
Reset khi offline rồi kết nối lại phải gửi được báo cáo đã commit. Chưa có server
thật hoặc flash/monitor: runtime BLOCKED, không ghi PASS.

Build Phase 9 ban đầu: BUILD VERIFIED, 0xc8610, cloud vẫn tắt trong sdkconfig
cũ. Nhánh cloud bật cùng BLE được build lại ở Phase 10 để kiểm tra mã thực thi.

## Phase 10

File: `helmet_node/components/ble_comm/`, `bike_node/components/ble_comm/`,
`bike_node/main/bike_app.c`, `bike_node/components/relay_driver/`,
`shared/protocol/helmet_bike_codec.h`, sdkconfig.defaults của hai node.
UUID, version và các trường packet trong `helmet_bike_protocol.h` giữ nguyên.
Codec 8 byte little-endian tránh phụ thuộc padding khi truyền qua BLE.

Helmet advertising service UUID hiện có, status READ/NOTIFY và command WRITE.
Notify mỗi giây và khi đổi state. Command không có quyền ghi đè quyết định FSM.
Bike scan service, discover characteristics và CCCD, ghi subscribe rồi nhận notify.
Sai version/độ dài/state/flags hoặc sequence lùi: đóng kết nối. Packet trùng không
gia hạn timeout. Sequence wrap 255→0 và uptime wrap được hỗ trợ. Reset Helmet
sẽ làm kết nối đóng và Bike scan lại với backoff 1→2→4→8→16→30 giây.

Chỉ task ứng dụng Bike sửa `bike_state` và relay, chu kỳ 20 ms. Callback BLE
chỉ xử lý GATT và snapshot được bảo vệ. Sau sự kiện disconnect, quyền bị thu hồi
ở vòng ứng dụng kế tiếp. Khi radio mất mà chưa có disconnect, packet quá hạn
3000 ms sẽ thu hồi quyền; không chờ hết BLE supervision timeout. Relay không
ghi/log lại nếu trạng thái không đổi.

RC522 vẫn là skeleton và allowlist luôn false vì chưa có UID thật. Không thay
UID giả để mở khóa. OLED bắt buộc theo yêu cầu trước nhưng chưa có driver trong
phạm vi Phase 8–11 này; cần xác nhận controller/kích thước màn hình để hoàn thiện.
BLE hiện lọc theo service UUID, chưa có cơ chế xác thực/bonding với một Helmet
đã đăng ký; đây là thử nghiệm tải mô phỏng, không phải khóa chống giả mạo.

Cấu hình mặc định mới bật `CONFIG_BT_ENABLED`, `CONFIG_BT_NIMBLE_ENABLED` ở cả
hai node; Helmet bật thêm `CONFIG_SMART_HELMET_BLE_ENABLE` và
`CONFIG_SMART_HELMET_CLOUD_ENABLE`. Wi-Fi vẫn cần cấu hình người dùng. SDKCONFIG
cũ không tự nhận mọi thay đổi defaults: kiểm tra bằng menuconfig nếu dùng checkout
khác. Không đưa sdkconfig chứa thông tin đăng nhập lên Git.

## Phase 11 – Checklist trên thiết bị

Trước mỗi lần thử ghi ngày, firmware SHA256, board/port, cấu hình, tải mô phỏng,
log bắt đầu/kết thúc và số đo GPIO. Không ghi token/password vào báo cáo.

Từ từng thư mục node, sau khi chọn đúng cổng:

```powershell
idf.py -p <PORT_CUA_NODE> flash monitor
```

| Tình huống | Cách kiểm tra và tiêu chí PASS | Hiện tại |
|---|---|---|
| 1. Helmet offline | Tắt hotspot, cảm biến thật; warm-up 60 giây, PASS mới READY/DRIVING; ADC lỗi không cấp quyền; GPS/IMU/SOS vẫn hoạt động đồng thời | BLOCKED |
| 2. SOS vật lý | Nhấn GPIO6 xuống GND trong warm-up, nhấn ngắn/giữ 5 giây/thả/nhấn lại; đúng một event mỗi lần, EMERGENCY, LED/còi đúng mẫu | BLOCKED |
| 3. Va chạm/té ngã | Dùng jig thử có kiểm soát theo Phase 5, không đập mũ hoặc gây nguy hiểm; log IMU/FALL → ACCIDENT → EMERGENCY và báo cáo đúng nguồn sự kiện | BLOCKED |
| 4. GPS | Thử không fix/có fix, ngắt UART >5 giây; valid/lat/lon/sats/hdop/age phù hợp; không có fix giả hoặc nhiều task đọc UART | BLOCKED |
| 5. Wi-Fi | Tắt hotspot lâu hơn chuỗi retry rồi bật lại; kết nối trở lại không reset, báo cáo NVS vẫn còn | BLOCKED |
| 6. HTTP | Server thử trả 200/204, 400, 500, timeout; chỉ 2xx xóa bản tin, các lỗi có backoff tối đa 60 giây, không gửi dồn | BLOCKED |
| 7. BLE | Bật hai board, xác nhận connected/discover/subscribed; tắt/reset Helmet rồi bật lại, tự nối lại; notify state/sequence/uptime đúng | BLOCKED |
| 8. Relay fail-closed | Đo GPIO1 LOW khi boot, không có UID, BLE mất, packet lỗi/quá hạn; nhánh bật cần RC522/UID thật và đủ bốn điều kiện | BLOCKED |
| 9. Queue đầy/NVS | Tắt mạng, nhấn-thả SOS nhiều hơn queue length; có log thay bản cũ nhất, giữ bản mới; reset rồi khôi phục mạng phải gửi bản đã commit | BLOCKED |
| 10. Soak ≥30 phút | Ghi heartbeat, free/min heap, stack FSM; không reset/panic/watchdog, không giảm heap liên tục; lặp SOS và BLE/Wi-Fi reconnect | BLOCKED |

Đánh FAIL nếu không đáp ứng tiêu chí đo được; không ghi PASS chỉ vì không có lỗi
build. NVS full/commit lỗi phải giữ báo cáo chờ ở RAM và log, không erase toàn bộ
NVS. Khi queue đầy, việc thay bản cũ nhất là ngoại lệ mất bản tin có chủ đích đã
ghi ở chính sách; dung lượng hữu hạn không thể giữ mọi cảnh báo vô hạn.

## Kiểm thử phần mềm và bằng chứng

Chạy từ root repository:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File helmet_node/hardware_tests/host/run.ps1
```

Runner dùng GCC C11 `-Wall -Wextra -Werror`, compile trực tiếp source thực
`bike_state.c`, `system_state.c`, `helmet_io.c`, `gps_driver.c` với stub GPIO/IDF.
Kết quả thực tế: **PASS: 85 safety checks (host mocks, not hardware)**.
Runner còn compile source `cloud_client.c` với cJSON thật của ESP-IDF và fake
HTTP/NVS/scheduler: **PASS: 75 cloud checks (mock HTTP/NVS, no network)**.
Các lỗi được mô phỏng: offline, HTTP 301/400/500, timeout, commit thất bại,
ACK 204, queue đầy giữ bản mới, khôi phục blob sau reset mô phỏng, JSON escaping
và retry đạt trần 60 giây. Đây không phải kết quả thử HTTP hoặc flash vật lý.
Bao gồm 16 tổ hợp xác thực, fail-closed lúc boot/disconnect, FSM/SOS, packet
ngắn/dài/sai version, sequence wrap/lùi/trùng, debounce/bounce/giữ/thả/queue-full,
còi non-blocking và parser GPS checksum/tọa độ lỗi. Không mô phỏng radio, ADC,
HTTP server, NVS power-loss hoặc thời gian chạy 30 phút.

Build ESP-IDF 5.5.4 thực tế đã bật NimBLE hai node và cloud ở Helmet. Build với
URL rỗng từng còn 1% app; build kiểm chứng nhánh HTTPS với URL
`https://example.invalid/emergency` phát hiện binary `0x131e90` vượt phân vùng
1 MiB `0x31e90` byte. URL này chỉ dùng build, không flash/gửi mạng, đã trả lại rỗng.
Đã chuyển Helmet sang `CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y`: mẫu có sẵn
ESP-IDF, app 1500 KiB, giữ nguyên NVS/PHY và app offset 0x10000. Không đổi GPIO
hoặc cấu hình dung lượng chip flash; lần flash tiếp theo cần flash cả partition
table bằng `idf.py flash`, không chỉ app. Sau sửa phân vùng, nhánh HTTPS đầy đủ
BUILD VERIFIED: `0x131eb0`, còn 18%; Bike BUILD VERIFIED: `0x7d430`, còn 51%.
Bản endpoint rỗng bàn giao BUILD VERIFIED: `0xfc7d0`, còn 33%. Không có compiler
warning trong các build thành công gần nhất. Lỗi thiếu dependency esp_timer ở
lượt trước đã sửa. Log build cuối: `helmet_node/build/log/idf_py_stdout_output_6036`,
`bike_node/build/log/idf_py_stdout_output_19280`; nhánh HTTPS:
`helmet_node/build/log/idf_py_stdout_output_6228`. Các file build/log được Git ignore.

IO ưu tiên còi khẩn cấp: kết quả MQ-3 đến sau khi đã SOS không được thay thế mẫu
còi khẩn cấp bằng tiếng bíp ngắn. Có host test cho tình huống này.

Kiểm tra serial ngày 2026-10-13: chỉ thấy cổng ảo và Bluetooth, không thấy ESP32
USB. Chưa flash/monitor, chưa có bằng chứng HARDWARE VERIFIED cho bản này.

| Phase | Source | Build | Hardware | Còn lại |
|---|---|---|---|---|
| 8 | Tích hợp luồng chính, IO/MQ-3/GPS thật, giữ mock | VERIFIED | BLOCKED | Board/log thực; MQ-3 cần hiệu chuẩn nếu đo nồng độ |
| 9 | HTTP 2xx, NVS, snapshot GPS, retry | VERIFIED cùng cloud bật ở Phase 10 | BLOCKED | Endpoint và server thử nghiệm |
| 10 | NimBLE peripheral/central, packet validation, relay gating | VERIFIED hai node | BLOCKED | RC522/UID; thử reconnect/radio; OLED còn thiếu |
| 11 | Host tests và checklist | 160 checks PASS | BLOCKED | Toàn bộ checklist vật lý và soak 30 phút |
