# Phase 7 Communication Report

Ngay cap nhat: 2026-10-09

Project: `D:\KLTN_ESP32\smart_helmet_iot`

## 1. Trang thai

Phase 7 source da duoc them va build thanh cong cho target `esp32s3`.
Trang thai runtime van la `BLOCKED` cho cac bai test that vi chua co:

- Wi-Fi SSID/password duoc cau hinh.
- Backend endpoint, token, payload contract va certificate policy rieng.
- Bike Node ESP32-C3 repository de test BLE end-to-end.
- Cong USB serial ESP32-S3 xac dinh an toan de flash/monitor.

Mac dinh cac tinh nang communication dang disabled trong `sdkconfig`:

- `CONFIG_SMART_HELMET_WIFI_ENABLE` chua bat.
- `CONFIG_SMART_HELMET_CLOUD_ENABLE` chua bat.
- `CONFIG_SMART_HELMET_BLE_ENABLE` chua bat.

Khong co Wi-Fi password, token hoac backend URL nao duoc hardcode trong source.

## 1.1. Kien truc truyen thong da chot

```text
ESP32-S3 Helmet Node
├── BLE GATT <-> ESP32-C3 Bike Node
│   └── RFID + Relay
└── Wi-Fi STA -> dien thoai hotspot -> Cloud Server
```

Quy uoc:

- Helmet Node la node chinh: doc cam bien, phat hien tai nan, quan ly FSM va
  quyet dinh emergency.
- Wi-Fi chi dung de di qua hotspot dien thoai len Cloud Server.
- BLE GATT chi dung cho Helmet Node <-> Bike Node.
- Dien thoai khong phai BLE fallback trong kien truc da chot.

## 2. File da tao

Wi-Fi manager:

- `components/wifi_manager/CMakeLists.txt`
- `components/wifi_manager/Kconfig.projbuild`
- `components/wifi_manager/include/wifi_manager.h`
- `components/wifi_manager/wifi_manager.c`

Cloud emergency client:

- `components/cloud_client/CMakeLists.txt`
- `components/cloud_client/include/cloud_client.h`
- `components/cloud_client/cloud_client.c`

BLE communication contract:

- `components/ble_comm/CMakeLists.txt`
- `components/ble_comm/include/ble_comm.h`
- `components/ble_comm/include/ble_protocol.h`
- `components/ble_comm/ble_comm.c`

## 3. File da sua

- `main/main.c`
- `main/CMakeLists.txt`
- `main/system_config.h`
- `components/event_manager/include/event_manager.h`
- `components/system_state/system_state.c`
- `sdkconfig` duoc CMake/Kconfig cap nhat them nhom cau hinh Phase 7.

## 4. Chuc nang da co trong source

### 4.1. Wi-Fi manager

`wifi_manager` ho tro:

- Khoi tao NVS, netif, event loop va Wi-Fi STA.
- Cau hinh SSID/password qua Kconfig, khong hardcode secret.
- Xu ly `WIFI_EVENT_STA_DISCONNECTED` va `IP_EVENT_STA_GOT_IP`.
- Publish `SYSTEM_EVENT_WIFI_CONNECTED` khi co IP.
- Publish `SYSTEM_EVENT_WIFI_LOST` khi mat ket noi sau khi da connected.
- Reconnect co gioi han bang `CONFIG_SMART_HELMET_WIFI_MAX_RETRY`.

Neu Wi-Fi chua enabled hoac SSID rong, module return true va log disabled de
khong chan cac phase cam bien.

### 4.2. Cloud emergency client

`cloud_client` ho tro:

- Task rieng va queue rieng cho emergency report, khong block
  `task_system_manager`.
- HTTP POST JSON toi `CONFIG_SMART_HELMET_CLOUD_URL`.
- Payload khai bao duong truyen `helmet_wifi_phone_hotspot_cloud`.
- Lay GPS fix moi nhat tu `gps_read_data()` khi gui report.
- Dung ESP-IDF certificate bundle cho HTTPS.
- Ho tro optional bearer token qua `CONFIG_SMART_HELMET_CLOUD_AUTH_TOKEN`.
- Khong log token.

Neu cloud disabled hoac endpoint rong, submit emergency la no-op an toan.

### 4.3. BLE contract

`ble_comm` hien co API contract:

- `ble_comm_init()`
- `ble_comm_notify_state(system_state_t state, uint32_t uptime_ms)`
- `ble_comm_send_emergency(const char *source_event, uint32_t uptime_ms)`
- `ble_comm_make_status_packet(system_state_t state, uint32_t uptime_ms)`

`ble_protocol.h` chot protocol contract toi thieu:

- Helmet ESP32-S3 la BLE Peripheral / GATT Server.
- Bike ESP32-C3 la BLE Central / GATT Client.
- 1 service UUID rieng cho Helmet.
- 1 status characteristic de Helmet notify state.
- 1 command characteristic de Bike ghi command/ack ve sau nay.
- Status packet co `version`, `state`, `flags`, `sequence`, `uptime_ms`.

Do `CONFIG_BT_ENABLED` dang off va chua co Bike Node repo, module hien la
compile-safe no-op. Day la ranh gioi de sau nay thay bang NimBLE GATT server
ma khong phinh `main.c`.

### 4.4. Main integration

Normal app path da them:

```text
app_main
-> event_manager_init
-> system_state_init
-> wifi_manager_init
-> ble_comm_init
-> cloud_client_init
-> accident_detector_init
-> gps_init
-> mq3_init
-> mpu6050_init_with_config
-> wifi_manager_start
-> task_imu_monitor
-> task_system_manager
```

`task_system_manager`:

- Notify BLE contract khi FSM state thay doi.
- Khi vao `SYSTEM_STATE_EMERGENCY_REPORTING`, submit emergency report cho
  cloud client va BLE contract.

`HARDWARE_TEST_MODE` da duoc dua ve `0` de firmware chay normal app thay vi
luon vao test Phase 3.

## 5. Build verification

Lenh da chay thanh cong:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "& 'C:\Espressif\tools\Microsoft.v5.5.4.PowerShell_profile.ps1'; idf.py build"
```

Ket qua:

- `Project build complete`.
- Target: `esp32s3`.
- App binary: `0x5b530` bytes.
- App partition con trong: `0xa4ad0` bytes, tuong duong 64%.

## 6. Chua duoc ket luan PASS phan cung

Phase 7 chua phai `HARDWARE PASS`. Can test that rieng:

- Bat Wi-Fi config bang `idf.py menuconfig`.
- Cau hinh SSID/password ngoai source control.
- Cau hinh endpoint HTTPS va token neu backend yeu cau.
- Flash board va quan sat log `WIFI_CONNECTED`, HTTP status 2xx khi emergency.
- Test sai password, mat AP, timeout DNS/TLS/HTTP, backend 4xx/5xx.
- Khi co Bike Node repo, thay BLE no-op bang NimBLE GATT server va test
  connect/notify/reconnect.
