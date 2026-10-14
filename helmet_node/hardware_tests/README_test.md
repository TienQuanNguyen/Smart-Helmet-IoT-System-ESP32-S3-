# DANH SÁCH KIỂM TRA PHẦN CỨNG GIAI ĐOẠN 1–6

Luồng ứng dụng tích hợp và checklist Phase 8–11 được ghi riêng tại
[báo cáo Phase 8–11](../docs/phase_8_to_11_validation.md). Không dùng kết quả
host mock hoặc build của luồng này để thay thế kết quả phần cứng Phase 1–6.

Thư mục này chứa các bài kiểm thử độc lập cho từng giai đoạn của Helmet Node
trên ESP32-S3. Mỗi lần biên dịch chỉ chạy một bài kiểm thử, vì vậy linh kiện
chưa kết nối ở giai đoạn khác sẽ không chặn bài kiểm thử đang chọn.

Các chuỗi nhật ký đặt trong dấu backtick được giữ nguyên tiếng Anh vì đó là nội
dung firmware thực tế xuất ra cổng nối tiếp và cần được đối chiếu chính xác.

## 1. Quy ước kết quả

- `BUILD VERIFIED`: mã của giai đoạn được chọn đã biên dịch thành công cho ESP32-S3.
- `MOCK VERIFIED`: kịch bản mô phỏng được chỉ định đã chạy thành công.
- `HARDWARE PASS`: phần cứng thật đã được chạy và đạt toàn bộ tiêu chí PASS.
- `FAILED`: có ít nhất một tiêu chí bắt buộc không đạt.
- `BLOCKED`: chưa thể chạy vì thiếu bo mạch, linh kiện, cổng COM hoặc cấu hình.

Biên dịch thành công không đồng nghĩa với `HARDWARE PASS`.

## 2. Cách chọn và chạy một giai đoạn

Mở `main/system_config.h` và đặt:

```c
#define HARDWARE_TEST_MODE 1
#define HARDWARE_TEST_PHASE <1..6>
```

Chỉ chọn một giai đoạn cho mỗi lần biên dịch.

### Biên dịch

Chạy tại thư mục `helmet_node`:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command `
  "& 'C:\Espressif\tools\Microsoft.v5.5.4.PowerShell_profile.ps1'; idf.py build"
```

### Nạp firmware và xem nhật ký

Xác định đúng cổng COM của ESP32-S3 rồi chạy:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command `
  "& 'C:\Espressif\tools\Microsoft.v5.5.4.PowerShell_profile.ps1'; idf.py -p COMx flash monitor"
```

Thay `COMx` bằng cổng thật của bo mạch. Không nạp thử vào một cổng COM chưa xác
định. Nhấn `Ctrl+]` để thoát trình theo dõi serial.

Sau khi hoàn tất kiểm thử, trả firmware về chế độ bình thường:

```c
#define HARDWARE_TEST_MODE 0
```

## 3. Bảng tổng quan

| Giai đoạn | Nội dung kiểm thử | Linh kiện ngoài |
|---|---|---|
| 1 | Khởi động ESP32-S3, bộ nhớ flash, heap và nhịp trạng thái | Không |
| 2 | MPU6050, I2C, WHO_AM_I, cấu hình và DATA_READY | MPU6050 |
| 3 | NEO-6M, UART, NMEA và phân tích GGA/GNGGA | NEO-6M |
| 4 | MQ-3, ánh xạ ADC, hiệu chuẩn, làm nóng và lấy mẫu | MQ-3, cầu chia áp, mạch đóng cắt nguồn |
| 5 | Phát hiện va chạm và ngã bằng MPU6050 thật | MPU6050 đã PASS Giai đoạn 2 |
| 6 | Hàng đợi sự kiện, hết thời gian chờ và FSM | Không |

---

## 4. Giai đoạn 1 – Kiểm tra cơ bản ESP32-S3

### Chuẩn bị

- Chỉ cần bo mạch ESP32-S3 và cáp USB.
- Không cần nối cảm biến.

### Chọn giai đoạn

```c
#define HARDWARE_TEST_MODE 1
#define HARDWARE_TEST_PHASE 1
```

### Danh sách thực hiện

- [ ] Biên dịch thành công và tạo được tệp `.bin`.
- [ ] Nạp firmware đúng cổng COM của ESP32-S3.
- [ ] Firmware khởi động và không rơi vào vòng lặp reset.
- [ ] Nhật ký có dòng `ESP32-S3 bring-up test`.
- [ ] Nhật ký hiển thị số lõi, phiên bản chip, dung lượng flash và heap còn trống.
- [ ] Không xuất hiện `FAIL: unable to read flash size`.
- [ ] Có ít nhất ba nhịp trạng thái liên tiếp, cách nhau khoảng 5 giây:

```text
PASS heartbeat: uptime=0s free_heap=... bytes
PASS heartbeat: uptime=5s free_heap=... bytes
PASS heartbeat: uptime=10s free_heap=... bytes
```

- [ ] `free_heap` luôn lớn hơn 0.
- [ ] Heap không giảm liên tục bất thường qua các nhịp trạng thái.
- [ ] Không có panic, watchdog reset hoặc khởi động lại ngoài dự kiến.

### Kết luận

Đánh dấu Giai đoạn 1 `HARDWARE PASS` khi đọc được flash và có ít nhất ba nhịp trạng thái
liên tiếp mà không có reset, panic hoặc suy giảm heap liên tục. Heartbeat dừng,
không tạo được tác vụ, không đọc được flash hoặc bo mạch khởi động lại bất thường là
`FAILED`.

---

## 5. Giai đoạn 2 – MPU6050, I2C và DATA_READY

### Nối dây

| MPU6050 | ESP32-S3 |
|---|---|
| VCC | 3.3 V |
| GND | GND |
| SDA | GPIO8 |
| SCL | GPIO9 |
| INT | GPIO7 |
| AD0 | GND cho địa chỉ `0x68`; 3.3 V cho địa chỉ `0x69` |

Không kéo SDA hoặc SCL lên điện áp cao hơn 3.3 V. Phải nối chung GND.

### Chọn giai đoạn

```c
#define HARDWARE_TEST_MODE 1
#define HARDWARE_TEST_PHASE 2
```

### Danh sách kiểm tra bus I2C

- [ ] Nhật ký khởi động đúng chân: SDA GPIO8, SCL GPIO9, INT GPIO7.
- [ ] Trạng thái rỗi của bus là HIGH/HIGH:

```text
I2C idle: SDA(GPIO8)=1 SCL(GPIO9)=1
```

- [ ] MPU6050 ACK tại `0x68` hoặc `0x69`.
- [ ] Quét bus tìm thấy ít nhất một thiết bị.
- [ ] Không có `ESP_ERR_TIMEOUT: bus busy/stuck`.
- [ ] Không có `FAIL: MPU6050 did not ACK at 0x68 or 0x69`.

### Danh sách kiểm tra nhận dạng và cấu hình MPU6050

- [ ] Thanh ghi `WHO_AM_I` tại `0x75` đọc được `0x68`.
- [ ] Các thanh ghi `CONFIG`, `SMPLRT_DIV`, `GYRO_CONFIG` và
      `ACCEL_CONFIG` đọc lại đúng giá trị vừa ghi.
- [ ] `ACCEL_CONFIG=0x08`, tương ứng dải đo `±4g`.
- [ ] Hệ số chuyển đổi gia tốc là `8192 LSB/g`.
- [ ] Gyroscope dùng dải `±250 dps`.
- [ ] Tần số lấy mẫu được cấu hình là 125 Hz.
- [ ] Có nhật ký tương đương:

```text
PASS: bus_address=0x68 WHO_AM_I=0x68
MPU configured: 125Hz, accel +/-4g, gyro +/-250dps
```

### Danh sách kiểm tra ngắt và dữ liệu chuyển động

- [ ] GPIO7 nhận ngắt DATA_READY liên tục.
- [ ] Không lặp lại lỗi `no DATA_READY interrupt on GPIO7 for 2000ms`.
- [ ] Có ít nhất ba nhật ký mẫu định kỳ dạng `PASS #...`.
- [ ] Khi đặt yên, tổng gia tốc gần `1g` trong sai số thực tế của module.
- [ ] Khi xoay hoặc di chuyển nhẹ, các trục accel/gyro thay đổi hợp lý.
- [ ] Dữ liệu không đứng yên bất thường tại một giá trị cố định.
- [ ] Không có lỗi đọc `INT_STATUS` hoặc `motion read`.
- [ ] Chuyển động có kiểm soát nằm trong dải `±4g`; nếu chạm giới hạn phải ghi
      nhận là bão hòa, không coi giá trị vượt dải là hợp lệ.

Ví dụ nhật ký đạt:

```text
PASS #... ACC[g] X=... Y=... Z=... | A=... GYRO[dps] X=... Y=... Z=...
```

### Kết luận

Giai đoạn 2 chỉ `HARDWARE PASS` khi bus HIGH/HIGH, MPU ACK, `WHO_AM_I=0x68`, cấu
hình đọc lại đúng, DATA_READY hoạt động và có dữ liệu chuyển động hợp lý. Không
ACK, sai WHO_AM_I, sai scale, bus bị kẹt, mất ngắt hoặc lỗi đọc là `FAILED`.

---

## 6. Giai đoạn 3 – GPS NEO-6M, UART và GGA

### Nối dây

| NEO-6M | ESP32-S3 |
|---|---|
| TXD | GPIO17, chân RX của ESP32-S3 |
| RXD | GPIO18, chân TX của ESP32-S3; có thể bỏ nếu chỉ nhận dữ liệu |
| GND | GND |
| VCC | Nguồn phù hợp với đúng breakout NEO-6M đang dùng |

Cấu hình UART: 9600 baud, 8 bit dữ liệu, không bit chẵn lẻ, 1 bit dừng. Đặt anten gần
cửa sổ hoặc ngoài trời, mặt anten hướng lên để kiểm tra fix vệ tinh.

### Chọn giai đoạn

```c
#define HARDWARE_TEST_MODE 1
#define HARDWARE_TEST_PHASE 3
```

### Danh sách kiểm tra nghiệm thu

- [ ] Bộ phân tích đạt `GPS PARSER PREFLIGHT: pass=5 fail=0` và không có
      `PREFLIGHT FAIL`.
- [ ] UART nhận liên tục các câu `NMEA: ...` trong 30 giây, không có
      `FAIL: GPS read error`.
- [ ] Có ít nhất một câu `GGA parsed: ...` được phân tích thành công.
- [ ] Khi anten ở nơi thoáng, GPS báo `fix=1`, `sats>0`, tọa độ hợp lệ và xuất
      hiện `HARDWARE PASS CANDIDATE: valid GPS fix received`.

`fix=0` chỉ xác nhận UART và bộ phân tích hoạt động, chưa đủ để kết luận GPS
định vị PASS.

### Kết luận

Giai đoạn 3 chỉ `HARDWARE PASS` khi đạt đủ bốn mục trên. Không có NMEA, lỗi
UART, kiểm tra sơ bộ thất bại hoặc không có vị trí hợp lệ là `FAILED`.

---

## 7. Giai đoạn 4 – MQ-3, ADC và lấy mẫu

### Nối dây và giới hạn điện

| MQ-3/mạch nguồn | ESP32-S3 |
|---|---|
| VCC bộ nung | Nguồn phù hợp, thường là 5 V; không lấy từ GPIO |
| GND | GND chung |
| AO | Qua cầu chia áp rồi vào GPIO4 |
| DO | Không sử dụng |
| EN mạch MOSFET/công tắc tải | GPIO5 |

Cầu chia áp đề xuất cho đầu ra AO có thể lên 5 V:

```text
MQ-3 AO ---- 10 kΩ ----+---- GPIO4
                       |
                     15 kΩ
                       |
                      GND
```

Với cầu chia trên, 5 V tại AO được giảm còn khoảng 3 V tại GPIO4.

- [ ] GPIO5 chỉ nối đến chân điều khiển của MOSFET/công tắc tải.
- [ ] Không cấp bộ nung MQ-3 trực tiếp từ GPIO5.
- [ ] Tất cả nguồn dùng chung GND.
- [ ] Đo bằng đồng hồ và xác nhận điện áp tại GPIO4 không vượt quá 3.3 V trước
      khi nối vào ESP32-S3.

### Chọn giai đoạn

```c
#define HARDWARE_TEST_MODE 1
#define HARDWARE_TEST_PHASE 4
```

### Danh sách kiểm tra khởi tạo ADC và hiệu chuẩn

- [ ] Driver khởi tạo thành công và xác nhận
      `ADC mapping verified: GPIO4 -> ADC1 channel 3`.
- [ ] Hiệu chuẩn khớp đường cong của ESP-IDF khởi tạo thành công.
- [ ] Chuyển đổi giá trị thô `0`, `2048`, `4095` tạo điện áp tăng đơn điệu.
- [ ] Có nhật ký `PREFLIGHT PASS: calibrated conversion ...`.
- [ ] Không có `MQ-3 initialization failed`.

### Danh sách kiểm tra làm nóng và lấy mẫu

- [ ] GPIO5 bật đúng mạch đóng cắt nguồn ngoài.
- [ ] Hoàn thành đủ 60 giây làm nóng.
- [ ] Có nhật ký:

```text
Warm-up complete; starting ADC samples
```

- [ ] Có ít nhất 10 mẫu trung bình liên tiếp:

```text
SAMPLE: raw=... ADC-node average=...V alcohol=...
```

- [ ] Không có `FAIL: MQ-3 ADC read error`.
- [ ] Giá trị thô không kẹt liên tục ở 0 hoặc 4095.
- [ ] Điện áp `ADC-node average` không vượt 3.3 V.
- [ ] Giá trị tương đối ổn định trong không khí sạch.
- [ ] Khi đưa hơi cồn lại gần có kiểm soát, giá trị thay đổi rõ ràng.
- [ ] Khi đưa nguồn cồn ra xa, giá trị có xu hướng trở lại mức nền.

Không đổ cồn trực tiếp lên cảm biến, không dùng lửa và phải thử ở nơi thông
thoáng.

### Kết luận

Giai đoạn 4 `HARDWARE PASS` khi ánh xạ ADC và hiệu chuẩn PASS, làm nóng đủ, có ít
nhất 10 mẫu hợp lệ, ADC không kẹt tại biên và cảm biến phản ứng hợp lý. Cờ
`alcohol=0/1` không tự chứng minh nồng độ cồn vì ngưỡng `1.80 V` hiện chưa được
hiệu chuẩn theo nồng độ thực tế.

Điện áp vượt 3.3 V, lấy dòng bộ nung từ GPIO, lỗi ADC hoặc dữ liệu kẹt tại biên là
`FAILED` và phải ngắt nguồn để kiểm tra lại dây.

---

## 8. Giai đoạn 5 – Phát hiện va chạm và ngã

Giai đoạn 2 phải `HARDWARE PASS` trước vì Giai đoạn 5 sử dụng MPU6050 thật. Chỉ thực
hiện chuyển động bằng tay có kiểm soát; không thả rơi thiết bị và không mô phỏng
tai nạn thật.

### Nối dây

| MPU6050 | ESP32-S3 |
|---|---|
| VCC | 3.3 V |
| GND | GND |
| SDA | GPIO8 |
| SCL | GPIO9 |
| AD0 | GND, địa chỉ `0x68` |
| INT | GPIO7; giữ nguyên dây từ Giai đoạn 2 |

Giai đoạn 5 đọc gia tốc theo chu kỳ nên không trực tiếp dùng tín hiệu INT, nhưng
nên giữ nguyên bộ dây Giai đoạn 2 đã xác nhận tốt.

### Chọn giai đoạn

```c
#define HARDWARE_TEST_MODE 1
#define HARDWARE_TEST_PHASE 5
```

### Danh sách kiểm tra sơ bộ bộ phát hiện

- [ ] `PREFLIGHT PASS: upright sample remains normal`.
- [ ] `PREFLIGHT PASS: impact detected`.
- [ ] `PREFLIGHT PASS: sustained tilt confirmed`.
- [ ] `PREFLIGHT PASS: second incident after reset`.
- [ ] Không có `PREFLIGHT FAIL`.
- [ ] Không có `MPU6050 or detector initialization failed`.

Kiểm tra sơ bộ chỉ xác nhận thuật toán với dữ liệu định sẵn; chưa thay thế kiểm tra
cảm biến thật bên dưới.

### Danh sách kiểm tra trạng thái bình thường

Đặt MPU6050 nằm yên, mặt bo mạch gần nằm ngang:

- [ ] Có nhật ký `sample` khoảng mỗi 500 ms.
- [ ] `magnitude` gần `1.0g` trong sai số thực tế.
- [ ] `tilt` gần `0 deg` khi trục Z thẳng đứng.
- [ ] Không báo `PASS IMPACT` liên tục khi nằm yên.
- [ ] Không báo `PASS FALL` khi nằm yên.
- [ ] Không lặp lỗi `sensor/detector update error`.

Ví dụ:

```text
sample: magnitude=1.00g tilt=0.0deg
```

### Danh sách kiểm tra va chạm có kiểm soát

Ngưỡng va chạm hiện tại là tổng gia tốc lớn hơn `3.0g`.

1. Giữ chắc module và dây nối.
2. Tạo một chuyển động cổ tay ngắn, nhanh nhưng có kiểm soát.
3. Không đập module và không thả rơi bo mạch.

- [ ] Có ít nhất một nhật ký `PASS IMPACT: magnitude=...g`.
- [ ] Magnitude được báo lớn hơn `3.0g`.
- [ ] Sau chuyển động, giá trị trở lại gần `1g`.
- [ ] Không tiếp tục báo va chạm khi module đã nằm yên.

### Danh sách kiểm tra nghiêng kéo dài

Điều kiện ngã hiện tại là góc nghiêng lớn hơn `60 deg` trong ít nhất `2000 ms`.

1. Đặt module nằm ngang và chờ dữ liệu ổn định.
2. Xoay module khoảng 90 độ.
3. Giữ nguyên tư thế ít nhất 3 giây.

- [ ] Không báo ngã ngay khi vừa bắt đầu nghiêng.
- [ ] Sau hơn 2 giây có `PASS FALL: tilt=...deg`.
- [ ] Góc được báo lớn hơn `60 deg`.
- [ ] Đưa module về ngang rồi lặp lại; lần thứ hai vẫn phát hiện được.

### Kết luận

Giai đoạn 5 chỉ `HARDWARE PASS` khi toàn bộ kiểm tra sơ bộ PASS, MPU6050 đọc liên tục,
trạng thái bình thường không báo giả, chuyển động kiểm soát tạo va chạm và góc
nghiêng kéo dài tạo ngã đúng thời gian. Lỗi I2C/cảm biến, báo giả liên tục hoặc
bỏ sót tình huống kiểm soát là `FAILED`.

---

## 9. Giai đoạn 6 – Hàng đợi sự kiện và FSM

Giai đoạn 6 không cần cảm biến. Chỉ cần ESP32-S3 và cáp USB. Có thể tháo GPS,
MPU6050 và MQ-3 vì bài test không đọc các module này.

### Chọn giai đoạn

```c
#define HARDWARE_TEST_MODE 1
#define HARDWARE_TEST_PHASE 6
```

### Danh sách kiểm tra hàng đợi và FSM

- [ ] Bộ quản lý sự kiện và trạng thái hệ thống khởi tạo thành công.
- [ ] Trạng thái đầu tiên là `STARTUP_ALCOHOL_CHECK`.
- [ ] `SYSTEM_EVENT_ALCOHOL_PASS` được gửi và nhận lại đúng.
- [ ] Alcohol pass chuyển FSM sang `READY_TO_RIDE`.
- [ ] Hàng đợi rỗng hết thời gian chờ đúng và không sinh sự kiện giả.
- [ ] Hết thời gian chờ tại READY chuyển FSM sang `DRIVING_MONITORING`.
- [ ] `SYSTEM_EVENT_IMU_IMPACT` chuyển FSM sang `ACCIDENT_DETECTED`.
- [ ] Lần hết thời gian chờ tiếp theo chuyển FSM sang `EMERGENCY_REPORTING`.
- [ ] Sau khi reset FSM, `SYSTEM_EVENT_ALCOHOL_FAIL` chuyển sang
      `LOW_POWER_IDLE`.
- [ ] Sau khi reset FSM, `SYSTEM_EVENT_SOS_PRESSED` chuyển sang
      `EMERGENCY_REPORTING`.
- [ ] Sau khi reset FSM, `SYSTEM_EVENT_BATTERY_LOW` chuyển sang
      `LOW_POWER_IDLE`.
- [ ] Không có `FAIL queue event`.
- [ ] Không có `unexpected event`.
- [ ] Không có trạng thái nào khác nhau giữa `expected` và `actual`.

Các nhật ký chính mong đợi:

```text
PASS initial state: STARTUP_ALCOHOL_CHECK
PASS alcohol pass: READY_TO_RIDE
PASS ready timeout: empty queue timed out
PASS queue timeout: DRIVING_MONITORING
PASS impact: ACCIDENT_DETECTED
PASS accident timeout: empty queue timed out
PASS emergency reporting: EMERGENCY_REPORTING
PASS alcohol fail: LOW_POWER_IDLE
PASS SOS: EMERGENCY_REPORTING
PASS battery low: LOW_POWER_IDLE
```

### Kết luận

Dòng cuối phải là:

```text
FSM TEST COMPLETE: pass=15 fail=0
```

- [ ] Tổng kết là `pass=15 fail=0`.
- [ ] Không có lỗi hàng đợi, hết thời gian chờ bất thường hoặc sai trạng thái.

Giai đoạn 6 `HARDWARE PASS` khi tất cả phép kiểm tra đều PASS và `fail=0`. Bất kỳ
`fail>0` nào cũng là `FAILED`.

---

## 10. Kiểm thử phụ 20 – OLED SSD1306

Đây là bài chẩn đoán I2C phụ, không phải một giai đoạn của dự án.

### Nối dây

| SSD1306 | ESP32-S3 |
|---|---|
| VCC | 3.3 V |
| GND | GND |
| SDA | GPIO8 |
| SCL | GPIO9 |

### Chọn bài kiểm thử

```c
#define HARDWARE_TEST_MODE 1
#define HARDWARE_TEST_PHASE 20
```

### Danh sách kiểm tra

- [ ] SDA và SCL ở mức HIGH khi bus rỗi.
- [ ] OLED ACK tại địa chỉ `0x3C` hoặc `0x3D`.
- [ ] Khởi tạo và truyền framebuffer không có lỗi I2C.
- [ ] Màn hình hiển thị viền và chữ `I2C OK`.

Truyền framebuffer thành công nhưng màn hình không hiển thị đúng chưa được coi
là `HARDWARE PASS`.

## 11. Biên bản kết quả cho mỗi lần chạy

Ghi lại đầy đủ:

- [ ] Ngày giờ và người thực hiện.
- [ ] Commit Git hoặc mô tả thư mục làm việc đang thay đổi.
- [ ] Phiên bản ESP-IDF và target `esp32s3`.
- [ ] Giai đoạn được chọn.
- [ ] Mã bo mạch và mã module.
- [ ] Cổng COM đã xác minh.
- [ ] Sơ đồ dây và điện áp nguồn thực đo.
- [ ] Đoạn nhật ký serial quan trọng.
- [ ] Kết quả cuối: `HARDWARE PASS`, `FAILED` hoặc `BLOCKED`.

Không đánh dấu `HARDWARE PASS` nếu mới chỉ biên dịch thành công hoặc chưa quan sát
đủ nhật ký/phản ứng vật lý được yêu cầu.
