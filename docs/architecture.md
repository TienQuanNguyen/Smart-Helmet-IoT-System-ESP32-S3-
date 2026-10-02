# Current Firmware Architecture

Status date: 2026-10-02

This document describes source that exists in the repository. Planned modules
are identified as absent and are not presented as implemented functionality.

## 1. Target and framework

- Helmet MCU: ESP32-S3.
- Framework: ESP-IDF v5.x.
- Language: C.
- Concurrency: FreeRTOS tasks and queue.
- Current target in `sdkconfig`: `esp32s3`.

The requested system also includes an ESP32-C3 Bike Node, but no Bike Node
source is present in this repository.

## 2. Implemented components

| Layer | Component | Current responsibility |
|---|---|---|
| Application | `main` | Initialization, test-mode dispatch, IMU and system-manager tasks |
| Test support | `app_test` | Mock sensor samples and mock events |
| System | `event_manager` | FreeRTOS queue of `system_event_t` values |
| System | `system_state` | Baseline state transitions |
| Processing | `accident_detector` | Acceleration magnitude, tilt and fall duration |
| Driver | `mpu6050_driver` | Register-level I2C access, configurable range/filter/rate, register readback and range-aware conversion |
| Driver | `gps_driver` | UART reception and minimal GGA/GNGGA parsing |
| Driver | `mq3_driver` | GPIO-to-ADC validation, calibrated ADC-node voltage, averaging, threshold and power-enable output |
| Test component | `hardware_tests` | Independently selected Phase 1-6 and auxiliary OLED tests |

The directories `components/ble_comm`, `components/wifi_manager`, and
`components/power_manager` are empty. They are not built application features.

## 3. Current execution paths

### 3.1 Hardware-test path

```text
app_main
-> HARDWARE_TEST_MODE != 0
-> hardware_test_start(selected phase)
-> start only the selected test
-> return before normal application initialization
```

This path prevents an unavailable module from blocking an unrelated phase.

### 3.2 Normal application path

When `HARDWARE_TEST_MODE` is zero:

```text
app_main
-> event_manager_init
-> system_state_init
-> accident_detector_init
-> gps_init
-> mq3_init
-> mpu6050_init_with_config
-> optional mock startup checks
-> task_imu_monitor
-> task_system_manager
```

### 3.3 IMU event flow

```text
MPU6050 or mock accel sample
-> accident_detector_update
-> SYSTEM_EVENT_IMU_IMPACT or SYSTEM_EVENT_FALL_CONFIRMED
-> event_manager queue
-> task_system_manager
-> system_state_handle_event
```

### 3.4 Baseline FSM

```text
BOOT
-> STARTUP_ALCOHOL_CHECK
-> READY_TO_RIDE or LOW_POWER_IDLE
-> DRIVING_MONITORING
-> ACCIDENT_DETECTED
-> EMERGENCY_REPORTING
```

SOS routes to emergency reporting. Battery-low routes to low-power idle unless
the current state is already an emergency state.

The normal application does not yet perform a real MQ-3 warm-up/decision flow,
periodic GPS acquisition, GPIO SOS handling, LED/buzzer actions, BLE transfer,
Wi-Fi connection, or emergency cloud reporting.

## 4. Current pin map

| Function | ESP32-S3 pin |
|---|---:|
| I2C SDA | GPIO8 |
| I2C SCL | GPIO9 |
| MPU6050 INT | GPIO7 |
| GPS RX | GPIO17 |
| GPS TX | GPIO18 |
| MQ-3 ADC input | GPIO4 |
| MQ-3 external power-switch enable | GPIO5 |
| SOS button | GPIO6 |
| Buzzer | GPIO10 |
| Status LED | GPIO2 |

The pin table documents current configuration only. It is not evidence that a
physical module passed its test.

## 5. Current validation boundaries

- The last recorded MPU6050 board run reached the I2C bus but received no ACK
  at 0x68 or 0x69. This is `FAILED`, not `HARDWARE PASS`.
- GPS parsing has known robustness gaps, including checksum handling. CHANGE-013
  adds a test that exposes the gap but does not fix the parser.
- CHANGE-001 now derives and validates GPIO4 as ADC1 channel 3 and uses the
  ESP-IDF curve-fitting calibration result. Reported voltage is at the ESP32
  ADC node after the divider, not the original MQ-3 AO node. The 1.80 V
  threshold remains unchanged pending measured calibration data.
- CHANGE-002 now configures the production MPU6050 path for +/-4 g and converts
  acceleration with 8192 LSB/g. GPIO8/GPIO9, address handling, WHO_AM_I and the
  legacy I2C API are unchanged.
- BLE, Wi-Fi, cloud, Bike Node and power-management descriptions remain plans
  until corresponding built source exists.

## 6. Dependency direction

The intended direction is:

```text
Application
-> System State / Event
-> Processing
-> Driver
-> ESP-IDF / FreeRTOS / Hardware
```

Some current drivers privately include headers from `main`. Correcting that
dependency direction is CHANGE-014 and is deliberately not performed as part
of CHANGE-013.
