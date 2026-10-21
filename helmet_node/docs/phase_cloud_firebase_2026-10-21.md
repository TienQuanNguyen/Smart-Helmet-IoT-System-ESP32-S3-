# Phase Cloud Firebase — Smart Helmet

Ngày: 21/10/2026 (Asia/Saigon). Dự án: `D:\KLTN_ESP32\smart_helmet_iot`.

## Cấu hình và dữ liệu đầu vào

- Database: `https://smarthelmet-thesis-default-rtdb.asia-southeast1.firebasedatabase.app`
- Thiết bị: `Helmet_001`.
- Đã đối chiếu `key.txt` và `smarthelmet-thesis-default-rtdb-export (1).json` trong Downloads.
- REST dùng mục **ADMIN (AUTH)** dưới dạng database secret qua tham số `auth`; Web API key không thay thế thông tin xác thực này.
- Khóa được nhập vào `helmet_node/sdkconfig` cục bộ; Wi-Fi SSID/password hiện có được giữ lại. Không đưa giá trị khóa vào tài liệu, mã nguồn hoặc Git.
- `sdkconfig`, cấu hình build sinh ra và firmware chứa cấu hình này là dữ liệu cục bộ cần giữ kín. Việc Git bỏ qua file không đồng nghĩa firmware được mã hóa.

## Những phần đã triển khai

| Đường dẫn | Hành vi |
|---|---|
| `Devices/Helmet_001/telemetry` | PATCH trạng thái mới nhất, khoảng 5 giây khi online; HTTP timeout có thể kéo dài chu kỳ |
| `Events/Helmet_001/esp_<report_id>_<uptime_ms>` | PATCH cảnh báo SOS/va đập/té ngã được FSM gửi; giữ nguyên ID qua các lần retry |
| `Diagnostics/Helmet_001/codex_<uuid>` | Nhánh tạm dùng kiểm tra mạng từ PC; bản ghi thử đã được xóa và kiểm tra lại |

Telemetry có GPS `lat`, `lng`, `gps_fix`, `satellites`, `hdop`, `gps_age_ms`; trạng thái `state`, `uptime_ms`, `internet`, `is_falling`, `is_sos_pressed`; dữ liệu cảm biến `imu_valid`, `accel_mag_g`, `alcohol_valid`, `alcohol_raw`, `alcohol_voltage`, `alcohol_detected`; và `last_update` theo giờ server.

`alcohol_voltage` là điện áp tại ADC, không phải nồng độ cồn đã hiệu chuẩn. MQ-3 hiện đo trong bước kiểm tra khởi động; telemetry giữ kết quả lần đo này, không tuyên bố cảm biến được đo liên tục. Dữ liệu IMU lỗi/MQ-3 chưa hợp lệ được đánh dấu invalid và trường số tương ứng được gửi null để xóa giá trị cũ. `is_sos_pressed` biểu diễn SOS đã chốt trong trạng thái emergency, không phải mức điện tức thời của nút.

Khi GPS không có fix hợp lệ, gửi `gps_fix=false`, tọa độ bằng 0. Giao diện phải kiểm tra `gps_fix` trước khi đặt marker. Không diễn giải HDOP thành độ chính xác mét. Giá trị `internet=true` chỉ phản ánh lần ghi thành công gần nhất; giao diện phải dùng tuổi của `last_update` để nhận biết mất mạng, vì board mất mạng không thể ghi `internet=false` lên server. Có thể coi dữ liệu quá 30 giây là stale.

Sự kiện có các trường tương thích export: `type`, `sensor`, `severity`, `note`, `timestamp`, `lat`, `lng`; kèm `device`, `report_id`, `event`, `state`, `uptime_ms`, `gps` và `gps_fix`. `timestamp` là thời điểm Firebase nhận lần ghi gần nhất, không phải thời điểm sự cố khi board đang offline. Retry cập nhật cùng bản ghi nên không tạo thêm event, nhưng có thể cập nhật timestamp.

Hàng đợi NVS giữ nguyên định dạng v1 và chính sách cũ: tối đa 4 cảnh báo, đầy thì thay cảnh báo cũ nhất bằng cảnh báo mới. Chỉ xóa khỏi hàng đợi sau HTTP 2xx và NVS commit thành công. HTTP redirect, 401/403, lỗi server hoặc lỗi mạng giữ lại báo cáo. Không xóa NVS khi phát hiện lỗi/cấu trúc không tương thích. Telemetry không được ghi NVS; khi mạng phục hồi gửi trạng thái mới nhất. Chờ retry được chia nhỏ để tiếp tục phục vụ telemetry. Stack cloud task tăng từ 8 KiB lên 12 KiB để có chỗ cho payload/URL mới và TLS; cần đo headroom trên board khi soak test.

HTTPS xác minh bằng certificate bundle; SNTP `time.google.com` bắt đầu sau khi Wi-Fi nhận IP. Firebase đợi đồng hồ hợp lệ trước khi gửi. Không tắt xác minh TLS. Đường dẫn ID chỉ nhận chữ, số, `_`, `-`; URL chỉ nhận HTTPS root trên miền Firebase; khóa được URL-encode và redirect bị tắt.

## Ranh giới chức năng

Đây là phần **gửi dữ liệu và cảnh báo từ Helmet lên Firebase**. Không thay đổi GPIO, ngưỡng cảm biến, quy tắc FSM hay mã Bike Node. Những chỉnh sửa MPU6050/MQ-3/hardware test có sẵn của người dùng được giữ nguyên.

- `Devices/.../control`: chưa đọc hay thi hành lệnh khóa/mở từ cloud; không bỏ qua các điều kiện an toàn Helmet/alcohol/RFID.
- `TripHistory`, `Users`, `info/owner_uid`: không sửa.
- Các trường `battery`, `speed`, `gyro_mag_dps`, `accuracy`, `bluetooth`, `helmet_connected`, `lock`, `is_locked`, `alcohol_level` chưa có nguồn số đo/trạng thái phù hợp trong phần tích hợp này; không ghi giả, các giá trị sẵn có trong database không được coi là đã xác nhận bởi firmware mới.
- Chưa thêm event riêng cho alcohol fail; kết quả alcohol hiện nằm trong telemetry. Các event emergency theo FSM vẫn là SOS/IMU.
- Đường HTTP chung vẫn dùng POST/Bearer khi tắt lựa chọn Firebase.

## Tệp thay đổi

- `helmet_node/components/cloud_client/{cloud_client.c,include/cloud_client.h,CMakeLists.txt}`
- `helmet_node/components/wifi_manager/{wifi_manager.c,Kconfig.projbuild}`
- `helmet_node/main/main.c`
- `helmet_node/hardware_tests/host/{run.ps1,test_cloud.c,stubs/sdkconfig.h,stubs/esp_http_client.h,stubs/esp_timer.h}`
- `helmet_node/tools/configure_firebase.py`
- `helmet_node/docs/phase_cloud_firebase_2026-10-21.md`

## Kiểm chứng

- **IMPLEMENTED**: Firebase REST, telemetry, mapping event, xác thực, đồng bộ giờ và retry/NVS.
- **MOCK VERIFIED**: 101 safety checks; RFID allowlist và các tổ hợp Helmet authorization; 97 generic cloud checks; 182 Firebase cloud checks. HTTP/NVS được mock, không phải board.
- **LIVE CLOUD VERIFIED — PC**: xác thực GET info của thiết bị có sẵn; PATCH nhánh Diagnostics; GET đối chiếu dữ liệu và timestamp số từ server; DELETE và GET xác nhận bản ghi đã biến mất. Không sửa telemetry, event, control, users hoặc rules khi smoke test.
- **BUILD VERIFIED**: ESP-IDF v5.5.4, ESP32-S3, bật Firebase/Wi-Fi/BLE theo cấu hình cục bộ. Binary `0x133840` bytes, app partition `0x177000`, còn 18%. Không flash board.
- **HARDWARE**: chưa flash, chưa đo runtime ESP32 → Wi-Fi → Firebase. Chưa kết luận HARDWARE PASS.

## Build và cấu hình lại

Mở PowerShell:

```powershell
& 'C:\Espressif\tools\Microsoft.v5.5.4.PowerShell_profile.ps1'
Set-Location 'D:\KLTN_ESP32\smart_helmet_iot\helmet_node'
python tools\configure_firebase.py --key-file 'C:\Users\ACER\Downloads\key.txt' --database-url 'https://smarthelmet-thesis-default-rtdb.asia-southeast1.firebasedatabase.app/' --sdkconfig sdkconfig
idf.py build
```

Script yêu cầu `sdkconfig` đã có sẵn, giữ Wi-Fi đã cấu hình và chỉ cập nhật các mục cloud cần thiết. Nếu SSID còn trống, chạy `idf.py menuconfig` → `Smart Helmet Phase 7 Communication`. Dùng hotspot 2.4 GHz có Internet. `HARDWARE_TEST_MODE` phải là 0 để chạy ứng dụng tích hợp; nguồn hiện tại đang là 0. Trạng thái mock cảm biến hiện tại là 0.

Kiểm thử host:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File 'D:\KLTN_ESP32\smart_helmet_iot\helmet_node\hardware_tests\host\run.ps1'
```

Lặp lại smoke test PC nếu cần; script chỉ dùng Diagnostics và tự dọn bản ghi:

```powershell
python tools\configure_firebase.py --key-file 'C:\Users\ACER\Downloads\key.txt' --database-url 'https://smarthelmet-thesis-default-rtdb.asia-southeast1.firebasedatabase.app/' --smoke
```

## Kiểm thử trên board để xác nhận HARDWARE PASS

Thực hiện trên bàn thử, tách tải khởi động/động cơ; chọn đúng cổng ESP32-S3 Helmet, không nạp firmware này vào ESP32-C3 Bike. Không thay đổi dây/GPIO hoặc ngưỡng cảm biến cho bài kiểm tra cloud.

```powershell
# Thay COMx bằng cổng của ESP32-S3 Helmet đã xác định.
idf.py -p COMx flash monitor
```

1. **Online:** bật hotspot. Log cần có `Wi-Fi connected`; sau đồng bộ giờ có `Cloud write delivered, status=204` (hoặc 200). `Waiting for SNTP clock before Firebase TLS` có thể xuất hiện trước khi đồng bộ. Firebase `Devices/Helmet_001/telemetry/last_update` phải tăng, `state`/GPS/cảm biến khớp tình trạng thực. SSID có cấu hình chưa phải bằng chứng Wi-Fi hoạt động.
2. **GPS chưa fix:** xác nhận `gps_fix=false`; không dùng tọa độ 0 làm vị trí thật. Sau GPS fix thật mới đối chiếu vị trí/satellites.
3. **SOS:** nhấn nút SOS trên bàn thử. Đối chiếu log `SOS_PRESSED`, `EMERGENCY_REPORTING` và một event mới có `type=SOS`, `sensor=SOS button`. Ghi thời gian bấm và thời gian xuất hiện bản ghi.
4. **Offline/retry:** tắt hotspot, tạo một SOS; cần log `Report retained; retry in ... ms`. Bật lại và xác nhận cảnh báo được gửi. Việc UI báo offline phải dựa vào `last_update` quá hạn.
5. **NVS qua reboot:** khi offline, tạo SOS rồi chờ đủ lâu để log không còn `Emergency awaiting durable outbox acceptance`; khởi động lại board, bật hotspot và xác nhận event được gửi. Không xóa flash/NVS giữa hai bước. Mỗi lần bấm mới là sự kiện mới; retry của cùng sự kiện phải dùng cùng key.
6. **Soak:** chạy tối thiểu 30 phút, lưu heap/min_heap/stack từ log health, số lần reconnect, độ trễ cập nhật và lỗi TLS/HTTP. Không gọi HARDWARE PASS nếu thiếu các bằng chứng này.

Lưu log monitor bằng `Ctrl+T`, sau đó `Ctrl+L`; thoát bằng `Ctrl+]`. Bằng chứng mỗi lần chạy nên có: mã lần thử, thời gian, board/cổng COM, bản firmware, môi trường mạng, đường dẫn event, kết quả đo, PASS/FAIL/BLOCKED và log thô đã rà soát thông tin nhạy cảm.

## Tài liệu giao thức

- [Firebase REST authentication](https://firebase.google.com/docs/database/rest/auth): phân biệt OAuth Bearer, ID token và legacy database secret.
- [Firebase REST saving data](https://firebase.google.com/docs/database/rest/save-data): PATCH cập nhật các trường và server timestamp `{".sv":"timestamp"}` theo milliseconds.

## Sửa lỗi host runner trong môi trường ESP-IDF (21/10/2026)

Đã tái hiện lỗi `Host compilation failed` khi chạy runner sau khi nạp ESP-IDF PowerShell profile. `gcc --version` vẫn thành công, nhưng `cc1.exe --help` trả mã `-1073741511` (`0xC0000139`). PATH của profile đặt các thư mục Espressif trước MSYS2; có DLL trùng tên giữa các bộ công cụ. Khi ưu tiên thư mục GCC, cùng cc1 trả 0 và cả bốn nhóm kiểm thử PASS.

`run.ps1` hiện tìm đường dẫn gcc.exe thực, ưu tiên thư mục của nó trong PATH trong thời gian chạy test, gọi compiler bằng đường dẫn đầy đủ và khôi phục PATH bằng `finally`. Không sửa PATH hệ thống, không thay ESP-IDF profile, không yêu cầu tắt venv. Lỗi compile sau này có thêm exit code và compiler path. Chạy lại nguyên lệnh host test bên trên trong terminal `(venv)` hiện tại.
