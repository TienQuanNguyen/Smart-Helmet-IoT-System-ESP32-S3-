# Phase 12 — Power Manager

Ngày: 30/10/2026. ESP32-S3, ESP-IDF v5.5.4 đang cài, C/FreeRTOS.
Phạm vi được người dùng phê duyệt: Mock Mode, kiến trúc backend đo pin thật,
không xuất SOC/thời gian còn lại khi thiếu dữ liệu đáng tin cậy, API cho app.

## 1. Phần cứng đã biết và giới hạn

Người dùng mô tả Shield hai cell 18650, đầu ra 5V/2.2A và 3V/1A, tích hợp
sạc và bốn LED. Đây là thông tin được cung cấp, chưa phải số đo đã xác minh.
1S2P mới là khả năng; chưa xác nhận topology, IC đo, sense point hoặc fuel gauge.
Firmware không suy ra dung lượng từ bốn LED, không coi rail ổn áp 5 V là điện áp pin,
không tự gán GPIO đo pin và không thay đổi BMS/charger.

Backend thật chưa được cấu hình. Trạng thái mặc định: `disabled/unavailable`.
MOCK là nguồn tổng hợp, không đại diện pack đang dùng. Không có GPIO/ADC/I2C
nào được component truy cập ở hai chế độ hiện có.

## 2. Kiến trúc đã triển khai

```text
Application constructs immutable power_config_t
  -> power_manager (FreeRTOS priority 2, 5 s default, 3072-byte stack)
     -> Mock fixture OR user-supplied bounded read callback
     -> pure C model: validity / calibrated curve or gauge / runtime / hysteresis
     -> protected snapshot + non-blocking Event Manager publication
  -> system manager copies snapshot (no I/O while holding snapshot lock)
  -> cloud telemetry serializer -> existing Firebase HTTPS worker -> phone app
```

`power_manager_default_config()` không chứa topology, curve hoặc capacity giả.
API lifecycle: gọi init/start một lần khi khởi động; init lần hai bị từ chối;
start lặp lại không tạo thêm task. Cấu hình được copy, không đọc lại bảng từ caller.
read_context phải tồn tại suốt vòng đời firmware. Callback chạy trên power task,
không được tạo network request, reinstall/reset bus đang dùng hoặc chặn vô hạn.

`power_manager_get_snapshot()` lấy snapshot nhất quán và kiểm tra tuổi mẫu.
Tuổi mẫu dùng uptime monotonic, phép trừ unsigned hỗ trợ wrap 32-bit; không dùng
wall-clock/SNTP để tính thời gian pin. Freshness mặc định 15 s. Timestamp phải
là lúc đo; backend không được làm mới timestamp của sample cache cũ.
Mẫu có cùng timestamp không được dùng để tăng số mẫu xác nhận hoặc làm đầy
cửa sổ dòng trung bình. Read lỗi không tự chuyển sang Mock.

MODEL chỉ tính SOC khi `measurement_verified=true` và có gauge SOC hợp lệ
hoặc curve đã xác minh. Curve tăng đơn điệu, không nội suy ngoài dải.
Curve từ voltage chỉ là estimate theo điều kiện hiệu chuẩn, không phải số đo SOC trực tiếp.
Runtime cần thêm capacity hữu dụng đã xác minh và dòng ở phía pin, dương khi xả;
đợi ít nhất ba mẫu dòng mới, dùng EWMA hệ số 0.25. Dòng dưới minimum mặc định
10 mA, đang sạc, thiếu dữ liệu hoặc stale làm runtime không hợp lệ.

```text
remaining_minutes = usable_capacity_mAh * SOC_percent * 0.6 / average_discharge_mA
```

Không chia dòng đo ở rail 5 V cho dung lượng mAh phía cell. Muốn dùng số đo ở
phía converter cần mô hình năng lượng/hiệu suất được xác minh và adapter phù hợp.
Giá trị 0 trong struct khi invalid không phải số đo: consumer bắt buộc đọc validity.

## 3. Event và bảo toàn safety

Ngưỡng phần mềm mặc định: LOW <=20%, CRITICAL <=10%, hysteresis 3 điểm %, ba
mẫu mới liên tiếp. Đây là chính sách cảnh báo có thể cấu hình, không phải ngưỡng
cắt pin/BMS. Chưa có SOC hợp lệ thì không phát các event theo SOC.

- `SYSTEM_EVENT_BATTERY_LOW` được giữ nguyên ID.
- Thêm `SYSTEM_EVENT_BATTERY_CRITICAL` và `SYSTEM_EVENT_BATTERY_RECOVERED` ở cuối enum.
- Chỉ publish khi mức ổn định thay đổi; nếu queue đầy, thử lại ở chu kỳ sau,
  không busy-wait. Không phát RECOVERED chỉ vì sensor bị lỗi hoặc mẫu hết hạn.
- Battery warning không ép FSM sang LOW_POWER_IDLE. Đường IMPACT/FALL khi đang
  DRIVING và SOS vẫn hoạt động; warning trong ACCIDENT không ngăn tiến tới EMERGENCY.
- Không thay MPU6050 range, threshold, WHO_AM_I, GPIO, cadence 20 ms, GPS cadence,
  SOS, BLE UUID/layout/flags/cadence hoặc Bike code.

Không thêm low-battery buzzer pattern có thể ghi đè cảnh báo tai nạn. Event được
ghi log, mức pin đi qua API; cảnh báo giao diện thuộc app.

## 4. Tối ưu điện năng có giới hạn

Task ngủ giữa các lần đo; không polling liên tục. Giữ MQ-3 heater power gating
hiện có. Khi SOC thật hợp lệ và LOW/CRITICAL, telemetry thường dùng interval gấp
ba cấu hình hiện tại (mặc định 5 -> 15 s). Mock không thay đổi chính sách này.
ACCIDENT/EMERGENCY không áp dụng giãn telemetry. Outbox emergency có lịch gửi
riêng, không chờ deadline telemetry. Network vẫn có timeout/độ trễ vốn có.
Không tắt Wi-Fi/GPS/BLE/IMU; không bật DFS, light sleep hoặc deep sleep.

Giảm số lần gửi là hành vi đã triển khai; số mA/Wh tiết kiệm và ảnh hưởng latency
trên board vẫn chưa đo, không được báo HARDWARE PASS.

## 5. API cho ứng dụng điện thoại

Giữ đường HTTPS/Firebase đang có: `Devices/<device_id>/telemetry`.
Thêm object `power`, schema 1. Không đổi scalar `battery` cũ của database;
app phải đọc namespace mới và không fallback sang trường pin cũ thiếu provenance.

Ví dụ payload MOCK mặc định (chỉ minh họa dữ liệu tổng hợp):

```json
{
  "power": {
    "schema": 1,
    "source": "mock",
    "mock": true,
    "status": "ok",
    "level": "unknown",
    "age_ms": 1000,
    "voltage_valid": true,
    "current_valid": true,
    "soc_valid": false,
    "runtime_valid": false,
    "voltage_v": 3.75,
    "current_ma": 120,
    "soc_pct": null,
    "remaining_minutes": null
  }
}
```

Nguồn: `disabled`, `mock`, `external`. Status: `unavailable`, `ok`, `read_error`,
`stale`. Level: `unknown`, `normal`, `low`, `critical`.

JSON serializer loại NaN/Inf, hết hạn tại thời điểm gửi và trả null khi invalid.
Firebase lưu null bằng cách xóa field; app phải xử lý cả missing và null.
PATCH chứa toàn bộ child `power`, gồm validity, để thay snapshot nguồn cũ.
Routine buffer được nâng từ 1024 lên 2048 byte; overflow trả lỗi, không gửi JSON dở.
Cloud tắt hoặc không có mạng thì API chưa tới app; snapshot nội bộ vẫn dùng được.
Nhánh HTTP emergency generic hiện không có routine telemetry; phase này dùng
nhánh Firebase đã có, không tạo backend mới.

App hiển thị: nếu `mock=true`, gắn nhãn “Dữ liệu mô phỏng”; nếu soc/runtime không
valid, hiển thị “Chưa có dữ liệu”. Với minutes hợp lệ: giờ=floor(minutes/60),
phút=minutes%60. App còn phải kiểm tra thời gian `last_update` của telemetry:
`age_ms` trong database không tự tăng khi thiết bị mất mạng. Không nhúng database
secret của thiết bị vào app; dùng Firebase Auth/security rules theo backend hiện có.

Repo này không chứa app điện thoại. Đã tích hợp firmware/API, chưa xác minh UI
hoặc end-to-end cloud/phone bằng dữ liệu board. Không thực hiện write production
trong lượt kiểm thử host. Khi bật Mock trên thiết bị, dùng device_id thử riêng.

## 6. Bật Mock và chạy Phase 12

Giữ nguyên `sdkconfig` hiện tại: tạo bản sao cho test và dùng build directory riêng.
Lệnh sau chạy trong PowerShell thật, từ Helmet Node:

```powershell
& 'C:\Espressif\tools\Microsoft.v5.5.4.PowerShell_profile.ps1'
Set-Location 'D:\KLTN_ESP32\smart_helmet_iot\helmet_node'
Copy-Item -LiteralPath '.\sdkconfig' -Destination '.\sdkconfig.power_test'
idf.py -B build_power_test -D SDKCONFIG=sdkconfig.power_test menuconfig
# Smart Helmet Power Manager:
#   Enable synthetic battery samples = y
#   Run isolated Phase 12 power test = y
idf.py -B build_power_test -D SDKCONFIG=sdkconfig.power_test build
# Xác định COM bằng tháo/cắm ESP32-S3 rồi thay COMx:
idf.py -B build_power_test -D SDKCONFIG=sdkconfig.power_test -p COMx flash monitor
```

Không commit bản sao sdkconfig có credentials. Nếu file test đã tồn tại, dùng lại
hoặc đặt tên mới thay vì ghi đè cấu hình riêng của bạn.

Kỳ vọng log:

```text
PHASE12 MOCK ONLY: synthetic samples, not battery measurements
MOCK CHECKS: pass=11 fail=0; HARDWARE BATTERY: BLOCKED
source=mock status=ok voltage_valid=1 voltage=3.750V soc_valid=0 runtime_valid=0
```

Snapshot đầu tiên có thể unavailable trước khi task lấy mẫu. Phase 12 kiểm tra
model, event queue và FSM bằng vector tổng hợp, rồi chạy power task/monitor định kỳ.
Nó không khởi động normal application hoặc gửi cloud; không phải thử MPU/SOS vật lý.

Muốn test normal firmware + Mock API: tắt `Run isolated Phase 12`, giữ Mock bật,
chọn device_id test cùng cấu hình Firebase hợp lệ bằng menuconfig; giữ sensor mock
ở thiết lập hiện tại. Normal firmware mặc định tắt Mock và báo pin unavailable.

## 7. Backend thật và linh kiện đề xuất

`power_read_fn(context, timeout_ms, sample)` là điểm gắn adapter ADC/gauge/I2C.
Application chọn POWER_EXTERNAL và cung cấp callback, context, thông số đã xác minh.
Không có callback thì init bị từ chối; không có profile/gauge đáng tin thì SOC invalid.

ADC: adapter phải lấy mV từ ESP-IDF calibration, rồi dùng
`power_adc_divider_voltage(calibrated_mv, Rtop, Rbottom, max_node_mv, &battery_v)`.
Hàm kiểm tra finite/range, không tự gán reference 3.3 V, GPIO hoặc mở ADC.
MQ-3 đang sở hữu ADC1; không gọi `adc_oneshot_new_unit(ADC_UNIT_1)` lần hai.
Cần thống nhất ownership/driver chia sẻ nếu chọn ADC, và phê duyệt chân sense mới.
`max_node_mv`, điện trở và protection phải chọn theo pack maximum voltage đã đo.

Sơ đồ ADC nguyên lý, chưa chốt giá trị/chân:

```text
Protected pack + -- Rtop --+-- calibrated ADC input (GPIO chưa chọn)
                          |
                       Rbottom
                          |
Protected pack - -------- GND
```

INA226 là phương án khảo sát đo voltage/current: bus 0–36 V, logic supply
2.7–5.5 V theo TI. IC này không tự đo SOC. Module/shunt phải phù hợp peak current,
công suất, điện áp và đường xả/sạc của Shield. Dùng VS/pull-up 3.3 V khi nối ESP32.

```text
Protected pack + -- IN+ -- shunt -- IN- -- converter/load input
                                  |
                                VBUS
Protected pack - ---------------- GND chung
ESP32 3V3 ----------------------- VS
GPIO8 / GPIO9 ------------------- SDA / SCL (pull-up về 3V3)
```

Chỉ áp dụng nếu Shield cho phép truy cập đường protected pack trước converter;
không bỏ qua BMS hoặc sửa đường cell/charger tùy ý. Xác minh schematic, topology,
địa chỉ, shunt, logic levels trước đấu nối. Chưa khẳng định Shield hiện có điểm sense này.
Bus MPU6050 hiện là legacy I2C0/400 kHz; adapter phải dùng bus được owner khởi tạo,
không reconfigure/reinstall. Timeout callback <=10 ms là hợp đồng, phần mềm không
thể cưỡng chế một callback viết sai. Thử bus-stuck/NACK khi IMU hoạt động là bắt buộc;
low task priority không loại được rủi ro điện khi sensor kéo SDA/SCL thấp.

Nếu dùng fuel gauge: MAX17048 phù hợp một cell nối tiếp; MAX17049 cho hai cell
nối tiếp. Không chọn chỉ từ số viên 18650, và không mặc định Shield là 1S2P.
Tham khảo chính hãng:
- [TI INA226](https://www.ti.com/product/INA226)
- [MAX17048/MAX17049](https://www.analog.com/media/en/technical-documentation/data-sheets/max17048-max17049.pdf)
- [ESP-IDF ADC oneshot ownership](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/peripherals/adc_oneshot.html)

## 8. Kiểm chứng và nghiệm thu thực tế

Host tests compile source thật với GCC C11, `-Wall -Wextra -Werror`, giả lập
scheduler/HTTP/NVS và số đo. Chạy:

```powershell
Set-Location 'D:\KLTN_ESP32\smart_helmet_iot'
& '.\helmet_node\hardware_tests\host\run.ps1'
```

Kết quả host: safety 101 PASS; RFID authorization PASS; cloud 97 PASS;
Firebase 229 PASS; power/model/JSON/FSM 96 PASS; power task/snapshot/retry 39 PASS.
Bao phủ: thiếu cấu hình, curve boundaries, NaN/Inf, stale/wrap/cache, charging/zero
current, runtime validity, hysteresis/recovery, full-queue retry, JSON/overflow,
cloud throttling, SOS/impact/fall giữa battery warning. Không phải HARDWARE PASS.

Build từ repository thật bằng ESP-IDF v5.5.4, target esp32s3, exit code 0 cho cả
hai cấu hình. Bản normal POWER_MOCK=n / POWER_TEST=n; bản Phase 12 cả hai=y.
Normal còn 18% và Phase 12 còn 81% phân vùng app. Không có compiler warning/error
trong các log build đã kiểm tra. Build/cache và bản sao sdkconfig nằm trong work
của chat, không thay sdkconfig repository. Hardware execution chưa chạy.

| Cấu hình | Kết quả | Binary bytes | SHA-256 |
|---|---|---:|---|
| Normal | BUILD VERIFIED | 1263792 | `c95f77779abec8973c0601ece054ca7439d217e8a0dbfdd9de09ce42a3579833` |
| Phase 12 Mock | BUILD VERIFIED | 298592 | `7a5c32e1159794475ad23c018e6533789e26506b25a0a9699e44535ff1929fd2` |


| Ca thực tế | Dữ liệu cần ghi | Điều kiện PASS |
|---|---|---|
| Điện áp/dòng | DMM/reference và snapshot ở nhiều tải | Trong tolerance đã xác định từ sensor, shunt, divider và dụng cụ |
| Mất sensor | NACK, unplug, stale, recovery | Invalid rõ ràng, không fake SOC, không flood event |
| Nguồn yếu/cạn | Simulator -> LOW/CRITICAL/recover | Đúng ngưỡng/debounce, không ép FSM rời monitoring |
| Safety đồng thời | IMU cadence/jitter, SOS latency, BLE packet/connection | Không regression vượt ngân sách timing đã chốt so với baseline |
| Bus fault | SDA/SCL fault và IMU logs | Không ảnh hưởng vượt ngân sách; nếu fail phải sửa isolation/hardware trước triển khai thật |
| Cloud/app | Payload, last_update, UI, mất mạng/reconnect | Không hiển thị stale/mock như pin thật; emergency không bị lịch tiết kiệm chặn |
| Runtime/power | Current mean/peak, Wh, thời gian thực, estimate | Sai số theo tolerance được duyệt trước đo; có kết quả A/B |

Không cố xả cell xuống dưới giới hạn an toàn để test CRITICAL: dùng nguồn giới hạn
dòng/battery simulator phù hợp. Đo điện áp trước nối ESP32. Chưa có board log,
shield topology và calibration thì các mục trên là BLOCKED, không tự ghi PASS.
Metadata: board/shield/cell model, topology, firmware hash, backend/config, dụng cụ,
điện áp/dòng thực, trạng thái sạc, tải, sample interval, timestamp, min/mean/max,
latency và tên log. Không ghi credentials vào báo cáo.
