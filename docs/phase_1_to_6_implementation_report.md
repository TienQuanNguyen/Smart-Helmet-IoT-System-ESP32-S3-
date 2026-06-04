# Bao cao tong hop qua trinh trien khai Phase 1 den Phase 6

Ngay lap: 2026-06-04

Project: `D:\KLTN_ESP32\smart_helmet_iot`

Muc tieu hien tai:

- Chi target `ESP32-S3 DevKit`.
- Dung `ESP-IDF`, `FreeRTOS`, ngon ngu C.
- Khong dung Arduino API, Arduino IDE, SIM 4G, camera.
- Khong tich hop Wi-Fi/BLE trong cac phase hien tai.
- Firmware di theo huong module hoa bang ESP-IDF components.

## 1. Quy tac ky thuat da ap dung

Da giu cac gioi han sau trong code:

- Khong dung `Serial.begin`, `Serial.print`.
- Khong dung `Wire.begin`.
- Khong dung `analogRead`.
- Khong dung `delay`.
- Khong dung `pinMode`.
- Khong dung `digitalWrite`.
- Khong dung TinyGPS++.
- Khong tao cau hinh ESP32 thuong.
- Khong tao dual-target `esp32/esp32s3`.
- Khong hardcode Wi-Fi password, API key, token, Firebase key.
- Khong tich hop Wi-Fi/BLE/Phase 7.

API ESP-IDF dang duoc dung:

- GPIO: `gpio_config`, `gpio_set_level`.
- UART: `uart_param_config`, `uart_set_pin`, `uart_driver_install`, `uart_read_bytes`.
- ADC: `adc_oneshot`.
- I2C: `i2c_master_write_to_device`, `i2c_master_write_read_device`.
- FreeRTOS: `xTaskCreate`, `vTaskDelay`, `xQueueCreate`, `xQueueSend`, `xQueueReceive`.
- Logging: `ESP_LOGI`, `ESP_LOGW`, `ESP_LOGE`.

## 2. Phase 1 - Project bring-up

Trang thai: PASS.

Da kiem tra va cap nhat:

- `main/main.c` co `app_main()`.
- Co boot log bang `ESP_LOGI`.
- Co FreeRTOS task test.
- `main/pin_config.h` ton tai va chi chua pin ESP32-S3.
- `main/system_config.h` ton tai va chua cac tham so he thong.
- Project build duoc voi target `esp32s3`.

File lien quan:

- `main/main.c`
- `main/CMakeLists.txt`
- `main/pin_config.h`
- `main/system_config.h`
- `CMakeLists.txt`

### 2.1. Pin config ESP32-S3 only

`main/pin_config.h` da duoc dua ve ESP32-S3 only:

```c
#define PIN_I2C_SDA              8
#define PIN_I2C_SCL              9
#define PIN_GPS_RX               16
#define PIN_GPS_TX               17
#define PIN_MQ3_ADC              4
#define PIN_MQ3_POWER_EN         5
#define PIN_MPU6050_INT          7
#define PIN_BUZZER               18
#define PIN_LED_STATUS           2
#define PIN_SOS_BUTTON           6
```

Ghi chu:

- Khong con pin config cho ESP32 thuong.
- Khong dung `#if CONFIG_IDF_TARGET_ESP32`.

### 2.2. System config

`main/system_config.h` co cac macro chinh:

- `SYSTEM_NAME`
- `USE_MOCK_SENSOR_DATA`
- `IMU_SAMPLE_PERIOD_MS`
- `GPS_SAMPLE_PERIOD_MS`
- `POWER_TASK_PERIOD_MS`
- `MQ3_WARMUP_TIME_MS`
- `MQ3_SAMPLE_COUNT`
- `ACCIDENT_ACCEL_THRESHOLD_G`
- `FALL_TILT_THRESHOLD_DEG`
- `FALL_CONFIRM_TIME_MS`
- `MQ3_ALCOHOL_THRESHOLD_VOLTAGE`

Ghi chu:

- `USE_MOCK_SENSOR_DATA = 1` dung de test firmware tren ESP32-S3 khi chua co sensor that.
- Mock mode khong phai de ho tro ESP32 thuong.

## 3. Phase 2 - MPU6050 driver

Trang thai: PASS.

Component:

```text
components/mpu6050_driver/
├── CMakeLists.txt
├── include/
│   └── mpu6050_driver.h
└── mpu6050_driver.c
```

Da hoan thien:

- Co driver rieng cho MPU6050.
- Khong dung thu vien MPU6050 co san.
- Khong dung Arduino Wire.
- Dung ESP-IDF I2C master API.
- Co mock mode.
- Co API doc/ghi du lieu raw va convert sang don vi vat ly.

API hien co:

```c
bool mpu6050_init(void);
bool mpu6050_init_with_config(const mpu6050_config_t *config);
bool mpu6050_read_who_am_i(uint8_t *who_am_i);
bool mpu6050_read_raw(mpu6050_raw_t *raw);
bool mpu6050_read_accel_g(mpu6050_accel_t *accel);
bool mpu6050_read_gyro_dps(mpu6050_gyro_t *gyro);
```

Register da khai bao:

- `WHO_AM_I = 0x75`
- `PWR_MGMT_1 = 0x6B`
- `ACCEL_XOUT_H = 0x3B`
- `GYRO_XOUT_H = 0x43`

Ghi chu:

- Expected `WHO_AM_I = 0x68` neu chan AD0 cua MPU6050 noi GND.
- Default I2C pin trong driver da dong bo voi ESP32-S3: SDA `8`, SCL `9`.
- Mock mode tra gia tri accel/gyro hop ly de test task va accident detector.

## 4. Phase 3 - GPS driver

Trang thai: PASS.

Component:

```text
components/gps_driver/
├── CMakeLists.txt
├── include/
│   └── gps_driver.h
└── gps_driver.c
```

Da hoan thien:

- GPS module muc tieu: NEO-6M.
- UART GPS tu cau hinh bang ESP-IDF.
- Baudrate mac dinh: `9600`.
- GPS TXD noi ESP32-S3 UART RX GPIO16.
- GPS RXD noi ESP32-S3 UART TX GPIO17.
- Co test doc raw NMEA.
- Khong dung TinyGPS++.
- Khong dung Arduino Serial.
- Parser NMEA toi thieu dong goi trong `gps_driver`.
- Uu tien parse cau GGA.
- Ho tro `$GPGGA` va `$GNGGA`.
- Co kiem tra input NULL/chuoi qua dai/chuoi khong phai GGA.
- Co mock mode theo `USE_MOCK_SENSOR_DATA`.

API hien co:

```c
typedef struct {
    double latitude;
    double longitude;
    bool fix_valid;
    uint8_t satellites;
    float hdop;
} gps_data_t;

bool gps_init(void);
bool gps_init_with_config(const gps_config_t *config);
int gps_read_raw_line(char *buffer, size_t max_len, uint32_t timeout_ms);
bool gps_parse_nmea(const char *line, gps_data_t *out);
bool gps_read_data(gps_data_t *out);
```

Mock GPS:

- `gps_init()` return true khi mock enabled.
- `gps_read_raw_line()` tra ve chuoi GGA mock hop le.
- `gps_read_data()` tra ve:
  - `latitude = 10.8231`
  - `longitude = 106.6297`
  - `fix_valid = true`
  - `satellites = 8`
  - `hdop = 1.2`

Can validate that bang phan cung:

- NEO-6M.
- NEO-6M TXD -> ESP32-S3 GPIO16.
- NEO-6M RXD -> ESP32-S3 GPIO17.
- GND chung.
- Nguon cap phu hop module GPS.

## 5. Phase 4 - MQ-3 driver

Trang thai: PASS.

Component:

```text
components/mq3_driver/
├── CMakeLists.txt
├── include/
│   └── mq3_driver.h
└── mq3_driver.c
```

Da hoan thien:

- Dung ADC ESP-IDF `adc_oneshot`.
- Khong dung `analogRead`.
- Dieu khien MOSFET cap/ngat nguon MQ-3 bang GPIO `PIN_MQ3_POWER_EN`.
- Doc ADC raw.
- Tinh voltage tu raw ADC.
- Lay mau nhieu lan va loc trung binh.
- So sanh nguong voltage.
- Co mock mode theo `USE_MOCK_SENSOR_DATA`.

API hien co:

```c
bool mq3_init(void);
bool mq3_init_with_config(const mq3_config_t *config);
void mq3_power_on(void);
void mq3_power_off(void);
bool mq3_read_raw(uint16_t *adc_raw);
bool mq3_read_voltage(float *voltage);
bool mq3_sample_average(float *avg_voltage, uint16_t sample_count);
bool mq3_is_alcohol_detected(float voltage);
```

Luot do that du kien:

```text
mq3_power_on()
warm-up do state machine quan ly
mq3_sample_average()
mq3_is_alcohol_detected()
mq3_power_off()
```

Ghi chu:

- Driver khong block warm-up 60 giay.
- Warm-up se duoc quan ly bang state machine o layer cao hon.
- Real mode chi cho doc khi sensor da duoc `mq3_power_on()`.
- Mock mode co the tra raw/voltage sau init de phuc vu test.

Can validate that bang phan cung:

- MQ-3 analog out noi vao GPIO4/ADC channel tuong ung.
- MOSFET enable noi GPIO5.
- MQ-3 duoc cap nguon qua MOSFET.
- Can test lai nguong `MQ3_ALCOHOL_THRESHOLD_VOLTAGE` bang thuc nghiem.

## 6. Phase 5 - Accident detector

Trang thai: PASS.

Component moi:

```text
components/accident_detector/
├── CMakeLists.txt
├── include/
│   └── accident_detector.h
└── accident_detector.c
```

API da tao:

```c
typedef enum {
    ACCIDENT_STATUS_NORMAL = 0,
    ACCIDENT_STATUS_IMPACT_DETECTED,
    ACCIDENT_STATUS_FALL_CANDIDATE,
    ACCIDENT_STATUS_FALL_CONFIRMED
} accident_status_t;

typedef struct {
    float accel_magnitude_g;
    float tilt_angle_deg;
    bool impact_detected;
    bool fall_confirmed;
} accident_result_t;

bool accident_detector_init(void);
bool accident_detector_update(float ax_g, float ay_g, float az_g,
                              uint32_t timestamp_ms, accident_result_t *out);
accident_status_t accident_detector_get_status(void);
void accident_detector_reset(void);
```

Cong thuc:

```c
accel_magnitude_g = sqrtf(ax_g * ax_g + ay_g * ay_g + az_g * az_g)
tilt_angle_deg = atan2f(sqrtf(ax_g * ax_g + ay_g * ay_g), fabsf(az_g)) * 180 / PI
```

Logic:

- `impact_detected = true` neu magnitude > `ACCIDENT_ACCEL_THRESHOLD_G`.
- Fall candidate neu tilt > `FALL_TILT_THRESHOLD_DEG`.
- Fall confirmed neu fall candidate lien tuc qua `FALL_CONFIRM_TIME_MS`.
- `timestamp_ms` do caller truyen vao.
- Khong dung `vTaskDelay()` trong `accident_detector_update()`.
- Co reset state ro rang.

Mock test:

- `main.c` co mock accel sample khi `USE_MOCK_SENSOR_DATA = 1`.
- Mock co doan impact gia va doan tilt keo dai de test `SYSTEM_EVENT_IMU_IMPACT` va `SYSTEM_EVENT_FALL_CONFIRMED`.

## 7. Phase 6 - Event manager va System state

Trang thai: PASS.

### 7.1. Event manager

Component moi:

```text
components/event_manager/
├── CMakeLists.txt
├── include/
│   └── event_manager.h
└── event_manager.c
```

Co che:

- Dung FreeRTOS Queue.
- Queue giu thu tu event.
- `publish()` khong block vo han.
- `wait()` co timeout.
- Neu queue chua init thi return false va log loi.

API:

```c
bool event_manager_init(void);
bool event_manager_publish(system_event_t event);
bool event_manager_wait(system_event_t *event, uint32_t timeout_ms);
```

Event list:

- `SYSTEM_EVENT_NONE`
- `SYSTEM_EVENT_ALCOHOL_PASS`
- `SYSTEM_EVENT_ALCOHOL_FAIL`
- `SYSTEM_EVENT_IMU_IMPACT`
- `SYSTEM_EVENT_FALL_CONFIRMED`
- `SYSTEM_EVENT_GPS_VALID`
- `SYSTEM_EVENT_WIFI_CONNECTED`
- `SYSTEM_EVENT_WIFI_LOST`
- `SYSTEM_EVENT_BLE_CONNECTED`
- `SYSTEM_EVENT_SOS_PRESSED`
- `SYSTEM_EVENT_BATTERY_LOW`

Ghi chu:

- Co khai bao Wi-Fi/BLE event trong enum de dung sau nay, nhung Phase 6 chua tich hop Wi-Fi/BLE that.

### 7.2. System state

Component moi:

```text
components/system_state/
├── CMakeLists.txt
├── include/
│   └── system_state.h
└── system_state.c
```

API:

```c
bool system_state_init(void);
system_state_t system_state_get(void);
void system_state_set(system_state_t new_state);
void system_state_handle_event(system_event_t event);
const char *system_state_to_string(system_state_t state);
const char *system_event_to_string(system_event_t event);
```

State list:

- `SYSTEM_STATE_BOOT`
- `SYSTEM_STATE_STARTUP_ALCOHOL_CHECK`
- `SYSTEM_STATE_READY_TO_RIDE`
- `SYSTEM_STATE_DRIVING_MONITORING`
- `SYSTEM_STATE_ACCIDENT_DETECTED`
- `SYSTEM_STATE_EMERGENCY_REPORTING`
- `SYSTEM_STATE_LOW_POWER_IDLE`

Transition da ho tro:

- `BOOT -> STARTUP_ALCOHOL_CHECK` khi init.
- `STARTUP_ALCOHOL_CHECK + ALCOHOL_PASS -> READY_TO_RIDE`.
- `STARTUP_ALCOHOL_CHECK + ALCOHOL_FAIL -> LOW_POWER_IDLE`.
- `READY_TO_RIDE + NONE -> DRIVING_MONITORING`.
- `DRIVING_MONITORING + IMU_IMPACT -> ACCIDENT_DETECTED`.
- `DRIVING_MONITORING + FALL_CONFIRMED -> ACCIDENT_DETECTED`.
- `ACCIDENT_DETECTED -> EMERGENCY_REPORTING`.
- `SOS_PRESSED` o cac state chinh -> `EMERGENCY_REPORTING`.
- `BATTERY_LOW -> LOW_POWER_IDLE` neu khong dang emergency.

Ghi chu:

- Chua xu ly cloud.
- Chua xu ly Wi-Fi/BLE.
- `system_state_handle_event()` khong block.

## 8. main.c hien tai

`main/main.c` da duoc cap nhat theo huong test Phase 5/6 vua du:

Init trong `app_main()`:

- `event_manager_init()`
- `system_state_init()`
- `accident_detector_init()`
- `gps_init()`
- `mq3_init()`
- `mpu6050_init_with_config()`

Task da tao:

- `task_imu_monitor`
- `task_system_manager`

`task_imu_monitor`:

- Doc accel tu MPU6050.
- Neu mock mode bat, dung mock accel sample.
- Goi `accident_detector_update()`.
- Publish `SYSTEM_EVENT_IMU_IMPACT` khi impact.
- Publish `SYSTEM_EVENT_FALL_CONFIRMED` khi fall confirmed.
- Delay theo `IMU_SAMPLE_PERIOD_MS`.

`task_system_manager`:

- Cho event bang `event_manager_wait()`.
- Log event.
- Goi `system_state_handle_event()`.
- Log state hien tai.

Mock event:

- Khi `USE_MOCK_SENSOR_DATA = 1`, sau vai giay main se publish thu:
  - `SYSTEM_EVENT_ALCOHOL_PASS`
  - `SYSTEM_EVENT_SOS_PRESSED`

Khong lam:

- Khong tao Wi-Fi task.
- Khong tao BLE task.
- Khong gui server.
- Khong dieu khien relay that.

## 9. Cau hinh IDE/clangd

Da sua file:

```text
D:\KLTN_ESP32\.vscode\settings.json
```

Ly do:

- clangd o workspace root dang tro sai:

```text
d:\KLTN_ESP32\build
```

- Da sua thanh:

```text
d:\KLTN_ESP32\smart_helmet_iot\build
```

Muc dich:

- De IDE tim thay include path cua cac component moi, vi du:
  - `components/accident_detector/include`
  - `components/event_manager/include`
  - `components/system_state/include`

Neu VS Code van bao do:

- Chay `clangd: Restart language server`.
- Hoac dong/mo lai workspace.
- Hoac mo truc tiep folder `D:\KLTN_ESP32\smart_helmet_iot`.

## 10. Build/test

Lenh da chay thanh cong:

```powershell
& 'D:\esp\v5.5.4\esp-idf\export.ps1'; idf.py build
```

Ket qua:

- `Project build complete`.
- Sinh `build/smart_helmet_iot.bin`.
- App partition con trong khoang 77%.
- Target trong `sdkconfig`: `esp32s3`.

Lenh test khi co board:

```powershell
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

Khong de xuat:

- `idf.py set-target esp32`.
- Build cho ESP32 thuong.
- Pin config ESP32 thuong.

## 11. Danh sach file da tao/sua

File da tao:

- `components/gps_driver/CMakeLists.txt`
- `components/gps_driver/include/gps_driver.h`
- `components/gps_driver/gps_driver.c`
- `components/mq3_driver/CMakeLists.txt`
- `components/mq3_driver/include/mq3_driver.h`
- `components/mq3_driver/mq3_driver.c`
- `components/accident_detector/CMakeLists.txt`
- `components/accident_detector/include/accident_detector.h`
- `components/accident_detector/accident_detector.c`
- `components/event_manager/CMakeLists.txt`
- `components/event_manager/include/event_manager.h`
- `components/event_manager/event_manager.c`
- `components/system_state/CMakeLists.txt`
- `components/system_state/include/system_state.h`
- `components/system_state/system_state.c`

File da sua:

- `main/main.c`
- `main/CMakeLists.txt`
- `main/pin_config.h`
- `main/system_config.h`
- `components/mpu6050_driver/include/mpu6050_driver.h`
- `components/mpu6050_driver/mpu6050_driver.c`
- `D:\KLTN_ESP32\.vscode\settings.json`

## 12. Luu y quan trong cho cac phase tiep theo

- Chua lam Phase 7.
- Khong tich hop Wi-Fi/BLE truoc khi user yeu cau.
- MQ-3 warm-up 60 giay khong nen block trong driver; nen dua vao state machine.
- Can do thuc nghiem de chot nguong MQ-3.
- Can test GPS that ngoai troi hoac gan cua so de co fix.
- Can test MPU6050 WHO_AM_I voi AD0 noi GND, ky vong `0x68`.
- Can tiep tuc tach logic ung dung lon ra component rieng neu `main.c` bat dau phinh to.
