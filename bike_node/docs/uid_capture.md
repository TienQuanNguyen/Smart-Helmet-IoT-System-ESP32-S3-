# Đọc UID RC522 — Bike Node ESP32-C3

Ngày kiểm tra: 15/10/2026.
Dự án: D:\KLTN_ESP32\smart_helmet_iot\bike_node

## Thay đổi
Driver SPI hiện có đọc UID thật, chỉ nhận độ dài 4/7/10 byte.
Dòng `Card UID=... (N bytes)` được chuyển từ callback driver sang nhánh một lượt quét của bike_app. Giữ thẻ trong vùng đọc không in UID liên tục; nhánh quét được kích hoạt lại sau khi ứng dụng quan sát không có thẻ ít nhất 400 ms. Khi thao tác hãy lấy thẻ ra khoảng 1 giây để tính cả thời gian driver phát hiện mất thẻ.
Không thay GPIO, BLE, điều kiện relay hay cấu hình whitelist. Hai UID trong sdkconfig hiện đều rỗng; mọi thẻ bị từ chối, không tự đăng ký.
Mất giao tiếp kéo dài có thể được nhận là lấy thẻ ra; cần kiểm chứng độ ổn định với module thực tế.

## Đấu dây (ngắt nguồn trước khi đấu)

| RC522 | ESP32-C3 |
|---|---|
| SDA / SS | GPIO7 |
| SCK | GPIO4 |
| MISO | GPIO5 |
| MOSI | GPIO6 |
| RST | GPIO10 |
| 3.3V | 3.3V |
| GND | GND |
| IRQ | Để trống |

SDA trên module là chân CS của SPI. Dùng nguồn và logic 3.3V, không cấp 5V vào RC522. Kiểm tra trên bàn với tải mô phỏng; relay hiện dùng GPIO1, HIGH là khóa và LOW là mở.

## Nạp bản ứng dụng thông thường và đọc UID

Bản này giữ BLE hoạt động, không bật chế độ UID_SCAN_ONLY.
Mở ESP-IDF PowerShell và chạy:

```powershell
Set-Location 'D:\KLTN_ESP32\smart_helmet_iot\bike_node'
idf.py build
idf.py -p COM_BIKE flash monitor
```

Thay COM_BIKE bằng cổng của ESP32-C3. Thoát Monitor bằng Ctrl+].
Kiểm tra log khởi động:

```text
RC522 SPI ready; waiting for a card
RFID allowlist empty: all cards denied
```

Đưa riêng từng thẻ vào vùng đọc. Ví dụ định dạng (không phải UID của bạn):

```text
I (...) bike_app: Card UID=04:A1:B2:C3 (4 bytes)
W (...) bike_app: RFID DENIED: UID is not in allowlist
```

Giữ thẻ 5 giây: chỉ một dòng Card UID. Heartbeat/BLE vẫn có thể in log bình thường.
Lấy thẻ ra khoảng 1 giây rồi đưa lại: thêm đúng một dòng Card UID.
Lặp lại với móc khóa xanh. Không đặt hai thẻ cùng lúc.

## Tiêu chí kiểm tra trên phần cứng

- PASS: đọc UID và số byte, giữ thẻ không lặp UID, lấy ra rồi quét lại có dòng mới; whitelist rỗng và relay khóa.
- FAIL: không đọc được, UID thiếu byte, lặp UID khi giữ thẻ với kết nối ổn định, hoặc relay mở.
- BLOCKED: chưa nạp đúng firmware/chưa có module hoặc cổng kết nối.
- Hỗ trợ 7/10 byte được kiểm tra ở mã nguồn; hai thẻ đi kèm không nhất thiết có các độ dài này. Muốn xác nhận phần cứng cho cả ba loại cần thẻ thực tế tương ứng.

## Ghi lại và gửi UID

| Vật mang thẻ | UID đầy đủ | Số byte |
|---|---|---|
| Thẻ trắng | Chưa quét | Chưa biết |
| Móc khóa xanh | Chưa quét | Chưa biết |

Gửi nguyên hai dòng Card UID để đăng ký sau. Chưa nhập UID mẫu vào menuconfig.

## Kết quả kiểm tra trong lượt này

- PASS: build Bike ESP32-C3 với ESP-IDF 5.5.4, bản ứng dụng thông thường (BLE bật), binary 0x8d280 byte, còn 45% phân vùng ứng dụng.
- PASS: 101 host safety checks; bộ kiểm tra RFID allowlist và các tổ hợp điều kiện Helmet; 75 cloud checks mô phỏng.
- Chưa flash hoặc quét thẻ thực tế. Host tests không xác nhận giao tiếp SPI, độ ổn định đọc thẻ hay tác động relay trên board.
- Chỉ sửa rfid_reader.c và bike_app.c trong lượt này; giữ các thay đổi có sẵn trong dự án.
