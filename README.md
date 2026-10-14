# Smart Helmet IoT System

This repository contains two independent ESP-IDF projects plus a shared protocol
area:

```text
smart_helmet/
|-- helmet_node/
|   |-- CMakeLists.txt
|   |-- sdkconfig.defaults
|   |-- main/
|   |-- components/
|   `-- hardware_tests/
|
|-- bike_node/
|   |-- CMakeLists.txt
|   |-- sdkconfig.defaults
|   |-- main/
|   |-- components/
|   `-- hardware_tests/
|
|-- shared/
|   `-- protocol/
|
`-- README.md
```

## Projects

- `helmet_node/`: ESP32-S3 Helmet Node firmware and isolated hardware tests.
- `bike_node/`: ESP32-C3 NimBLE Central, authorization state and safe relay output.
  RC522 UID authorization and OLED remain incomplete.
- `shared/protocol/`: protocol definitions shared by Helmet and Bike nodes.

Build each node from its own project directory. A successful build is only
`BUILD VERIFIED`; hardware status requires flashing the correct board and
capturing real serial logs.

## Phase 8–11

Helmet now runs real MQ-3 warm-up/sampling, a single GPS reader with a protected
cache, SOS/LED/active-buzzer IO, a persistent NVS emergency outbox and NimBLE GATT.
Bike subscribes to Helmet status and revokes relay permission for lost/stale or
invalid data. No real RFID UID has been supplied: the relay stays locked.

See [Vietnamese validation report](helmet_node/docs/phase_8_to_11_validation.md)
for configuration, build evidence, host tests and the hardware checklist.
Neither board nor the server has been validated in this change. Do not use the
relay on a real vehicle. BLE service filtering is not authenticated pairing.
