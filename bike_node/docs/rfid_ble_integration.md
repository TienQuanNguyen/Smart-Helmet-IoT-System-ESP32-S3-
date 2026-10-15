# Quét UID RFID và chạy tích hợp Helmet–Bike

## Phạm vi

Helmet ESP32-S3 chạy NimBLE Peripheral/GATT Server; Bike ESP32-C3 chạy
NimBLE Central/GATT Client. Bike đăng ký nhận trạng thái Helmet, kiểm tra
phiên bản/gói tin/thứ tự và chỉ sử dụng dữ liệu mới trong 3 giây.
RC522 dùng thư viện [abobija/esp-idf-rc522](https://github.com/abobija/esp-idf-rc522), khóa commit trong
`components/rfid_reader/idf_component.yml` và `dependencies.lock`.

Chưa có UID thật: danh sách cấp quyền mặc định trống, mọi thẻ bị từ chối.
Không tự đăng ký thẻ đầu tiên, không có UID mẫu được cấp quyền trong firmware.
OLED chưa được triển khai trong phạm vi thay đổi này.

## Đối chiếu BikeNode.c

Đã tham khảo `C:\Users\ACER\Downloads\BikeNode.c`: dùng SPI2, mode 0,
100 kHz, reset RC522 và một lần quét tạo một yêu cầu mở/khóa.
Mã mới đọc đủ UID 4/7/10 byte, kiểm tra danh sách trước khi nhận yêu cầu.
Phần xử lý relay thuộc vòng lặp ứng dụng, callback RFID/BLE không ghi GPIO.

File tham khảo dùng ESP-NOW cho Helmet và BLE Bluedroid cho ứng dụng điện thoại.
Dự án hiện tại giữ giao thức NimBLE Helmet–Bike đã có, không đổi sang ESP-NOW.
Không tích hợp chế độ emergency-unlock bỏ qua Helmet/cồn của file tham khảo.

**Cực tính relay:** cấu hình active-low theo module đang dùng, GPIO1 LOW mở và HIGH khóa. Xác nhận
module thực tế trước khi nối tải demo. Không sử dụng bản này để cắt động cơ xe.

## Đấu dây

| RC522 | ESP32-C3 |
|---|---|
| SDA/SS (chân chọn SPI, không phải SDA I2C) | GPIO7 |
| SCK | GPIO4 |
| MISO | GPIO5 |
| MOSI | GPIO6 |
| RST | GPIO10 |
| 3.3V | 3.3V |
| GND | GND chung |
| IRQ | Không nối |

RC522 chỉ dùng nguồn và logic 3.3 V. Relay dùng GPIO1 theo `main/bike_config.h`.
Kiểm thử ban đầu với tải mô phỏng. Không thay chân theo ví dụ của thư viện.

## Lấy UID

Mở ESP-IDF PowerShell, vào `D:\KLTN_ESP32\smart_helmet_iot\bike_node`.
Giữ `BIKE_HARDWARE_TEST_MODE=0` trong `main/bike_config.h`.

```powershell
idf.py menuconfig
```

Vào `Bike RFID authorization`, bật `Read UIDs only (relay always locked)`.
Để trống cả hai UID, lưu rồi build. Thay `COM_BIKE` bằng cổng thật:

```powershell
idf.py build
idf.py -p COM_BIKE flash monitor
```

Đây là chế độ riêng: không khởi động BLE và không cho phép relay mở,
kể cả khi cấu hình có UID hợp lệ. Đưa từng thẻ vào vùng đọc. Log mong đợi:

```text
RC522 SPI ready; waiting for a card
UID SCAN ONLY: relay locked, BLE disabled
RFID allowlist empty: all cards denied
Card UID=04:A1:B2:C3 (4 bytes)
UID capture only: relay request denied
```

UID trên chỉ là ví dụ định dạng. Gửi UID thực tế thu được để cấu hình danh sách.
Thẻ 7/10 byte được in đủ byte, không cắt xuống 4 byte. UID là dữ liệu nhận dạng
thẻ, không phải khóa bí mật hay cơ chế chống sao chép thẻ.

Có thể build riêng chế độ quét mà không đổi cấu hình ứng dụng:

```powershell
idf.py -B build_uid_scan -D SDKCONFIG=build_uid_scan/sdkconfig -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.uid_scan.defaults' build
idf.py -B build_uid_scan -p COM_BIKE flash monitor
```

## Cấu hình và chạy cả hai node

1. Trong `menuconfig` của Bike, tắt chế độ chỉ đọc UID. Nhập UID thật vào
   `Authorized card UID 1` hoặc `2`, dạng `04:A1:B2:C3` hoặc `04A1B2C3`.
   Không nhập dấu cách. Chỉ chấp nhận UID 4, 7 hoặc 10 byte khớp hoàn toàn.
2. Build/nạp lại Bike bằng thư mục `build` thông thường.
3. Build/nạp Helmet từ `helmet_node`; `HARDWARE_TEST_MODE=0`, BLE và NimBLE bật.
   Dùng hai cửa sổ Monitor với đúng cổng riêng. Không dùng sự kiện cồn giả để
   ghi nhận kết quả phần cứng PASS.
4. Chờ Bike báo `Helmet connected; discovering service` và
   `Helmet notifications subscribed`. Helmet phải thực sự ở READY/DRIVING,
   có cờ alcohol PASS và không có cờ emergency.
5. Quét thẻ hợp lệ: `RFID request accepted: UNLOCK`, relay được mở.
   Lấy thẻ ra ít nhất 400 ms rồi quét lại: `RFID request accepted: LOCK`.
   Giữ thẻ trong vùng đọc không được làm relay bật/tắt liên tục.

Thẻ lạ không được gửi yêu cầu mở **hoặc khóa**; không thay đổi phiên đã mở.
Lấy thẻ ra không tự khóa. Điều kiện Helmet vẫn được kiểm tra mỗi vòng lặp:
mất BLE, dữ liệu quá hạn, trạng thái không sẵn sàng hoặc không còn alcohol PASS
sẽ thu hồi quyền mở. Kết nối lại không tự mở: phải lấy thẻ ra và quét lại.
Yêu cầu mở lúc Helmet chưa sẵn sàng bị từ chối, không được lưu chờ thực hiện sau.

## Tiêu chí kiểm thử thực tế

| Tình huống | Kết quả phải đạt |
|---|---|
| Khởi động/reset Bike | Relay khóa |
| Chế độ chỉ đọc UID, kể cả có UID trong danh sách | In UID; relay luôn khóa |
| Danh sách trống, quét bất kỳ thẻ nào | Từ chối; relay khóa |
| Thẻ lạ khi đang mở | Không tạo yêu cầu khóa/mở |
| Thẻ hợp lệ nhưng Helmet vắng/chưa ready/cồn fail | Từ chối mở |
| Thẻ hợp lệ + Helmet ready + alcohol PASS | Mở đúng một lần |
| Giữ nguyên thẻ trong vùng đọc | Không lặp yêu cầu |
| Bỏ thẻ ít nhất 400 ms, quét thẻ hợp lệ lại | Khóa |
| Tắt Helmet khi đang mở | Khóa khi mất liên kết hoặc dữ liệu hết hạn 3 giây, cộng chu kỳ xử lý |
| Bật lại Helmet | Tự kết nối lại; vẫn khóa đến lần quét hợp lệ mới |
| Helmet báo emergency hoặc không còn alcohol PASS | Khóa; không tự mở lại |
| Thẻ 4/7/10 byte | In và so khớp toàn bộ UID |
| RC522 không giao tiếp được lúc boot | Khởi tạo lỗi; relay vẫn khóa |

Lưu log của cả hai node, UID và video relay cho từng trường hợp. Build và host
tests không thay thế kiểm thử vô tuyến, RC522 và relay trên phần cứng.

## Kết quả kiểm chứng ngày 14/10/2026

ESP-IDF 5.5.4, checkout nền `9ebe08b` cùng thay đổi chưa commit:

| Kiểm tra | Kết quả |
|---|---|
| Build Helmet ESP32-S3 với NimBLE bật | PASS, binary 0xfc7d0 byte |
| Build Bike ESP32-C3 tích hợp RC522/BLE | PASS, binary 0x8d280 byte |
| Build Bike chỉ đọc UID, cấu hình riêng | PASS, binary 0x3f930 byte |
| Host safety tests | PASS, 101 kiểm tra |
| Host UID allowlist + yêu cầu mở/khóa + phục hồi BLE | PASS |
| Host cloud regression | PASS, 75 kiểm tra mô phỏng |
| Flash và RC522/BLE/relay trên bo thật | Chưa thực hiện |

Cả cấu hình Bike bình thường và chỉ quét UID đều có UID 1/2 rỗng.
NimBLE Helmet/Bike đã có trong checkout trước thay đổi này; phần bổ sung là
RC522 thật, đọc/in UID, danh sách cấp quyền và yêu cầu relay có xác thực.

SHA-256 của các binary đã build (không phải chứng nhận phần cứng):

```text
bike_node/build/bike_node.bin
C0FC49E83871DEC3892BD8F21BC38E1DAD625DF6BD95AD9BCAC30EEAC3F1A78F
bike_node/build_uid_scan/bike_node.bin
9CE6E1F67CFA8A6DEFC33935D514A121ECC55194605167BF25AA93833AFF6317
helmet_node/build/smart_helmet_iot.bin
83CF46964ED054CDAD1E5ED8C288ADD537662C14B6DB1A9CA1CC3B5430C98A2D
```
