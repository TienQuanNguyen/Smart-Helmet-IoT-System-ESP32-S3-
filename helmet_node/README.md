# Helmet Node - ESP32-S3

## Overview

Smart Helmet IoT System is an embedded IoT graduation project developed with
ESP32-S3, ESP-IDF, and FreeRTOS. The target system combines motion sensing,
alcohol checking, GPS location, wireless communication, and a simulated bike
start-lock circuit.

This ESP-IDF project contains Phase 1–6 isolated tests and the Phase 8–10 main
application: real sensor services, SOS/LED/buzzer, persistent cloud outbox and
NimBLE peripheral. The Bike Node lives in `../bike_node/`. Board/server runtime
and the 30-minute soak test remain unverified. The current evidence and Phase 11
checklist are in [the validation report](docs/phase_8_to_11_validation.md).

> The start-lock design is for a controlled academic simulation only. It must
> not be connected to a real motorcycle ECU, smart key, or starter system.

## Evidence labels

Project reports use these labels independently:

- `IMPLEMENTED`: source exists and was inspected.
- `BUILD VERIFIED`: the current source compiled successfully for ESP32-S3.
- `MOCK VERIFIED`: a mock scenario was executed and met its expected results.
- `HARDWARE PASS`: the named board/module was physically run and all stated
  PASS criteria were observed.
- `FAILED`: a test or inspection produced evidence that a criterion was not
  met.
- `BLOCKED`: required hardware, configuration, dependency, or external
  contract is unavailable, so the criterion cannot currently be evaluated.

`BUILD VERIFIED` and `MOCK VERIFIED` must never be reported as `HARDWARE PASS`.

## Target technical highlights

- Main MCU: ESP32-S3 with ESP-IDF and FreeRTOS.
- Firmware design: modular, event-driven components.
- Sensors: MPU6050, MQ-3, and a UART GPS module.
- Communication target: Wi-Fi as the main channel and BLE as a local/fallback
  channel.
- Backend target: a Node.js service that receives device data, processes
  alerts, and forwards location information to relatives.
- Bike Node target: ESP32-C3 controlling a relay/transistor simulation circuit.
- Power target: MOSFET control for the MQ-3 heater and system power policies.

## Target system architecture

```text
Helmet Node (ESP32-S3)
|
+-- MPU6050          -> Motion / fall detection
+-- MQ-3             -> Alcohol-level checking
+-- GPS module       -> Location tracking
+-- Wi-Fi / BLE      -> Alert and data communication
`-- Battery system   -> Portable power source

Bike Node (ESP32-C3, planned)
`-- Relay / transistor circuit
    `-- Simulated start-lock output

Backend (Node.js, planned)
`-- Receive emergency payloads and forward GPS alert information
```

## Hardware components

| Component | Purpose |
|---|---|
| ESP32-S3 DevKit | Main helmet controller |
| ESP32-C3 DevKit | Planned simulated bike node |
| MPU6050 | Motion and fall detection |
| MQ-3 | Alcohol detection |
| GPS module | Location tracking |
| Buzzer / LED | Warning output |
| Relay / transistor | Planned simulated start-lock control |
| MOSFET | MQ-3 power control |
| 18650 battery system | Portable power supply |

## Current firmware structure

```text
helmet_node/components/
+-- mpu6050_driver/
+-- gps_driver/
+-- mq3_driver/
+-- accident_detector/
+-- event_manager/
`-- system_state/

helmet_node/hardware_tests/
+-- phase1_board_test.c
+-- phase2_mpu6050_test.c
+-- phase3_gps_test.c
+-- phase4_mq3_test.c
+-- phase5_accident_test.c
`-- phase6_fsm_test.c
```

## Core logic

### Accident detection

The implemented detector consumes MPU6050 acceleration data to
identify high acceleration and excessive tilt. Physical validation still
depends on a working MPU6050 connection and board run.

### Alcohol checking

The MQ-3 path uses GPIO4 through a protected ADC input and GPIO5 only as the
control signal for an external MOSFET/load switch. The voltage threshold is
defined at the ESP32 ADC node after the external divider, not directly at the
MQ-3 AO pin.

### GPS alert notification

The GPS service owns UART and caches complete parsed GGA fixes. Emergency HTTP
reports capture the cached position, validity and age; the cloud worker never
reads UART. Reports are committed to NVS and retried until HTTP 2xx. The backend
and live end-to-end delivery still require validation.

### Simulated start-lock

The Bike Node and relay/transistor control are implemented separately in
`../bike_node/`. Helmet Node does not directly drive the bike relay.

## Current execution mode

The checked-in configuration selects the real main application:

```c
#define HARDWARE_TEST_MODE 0
#define USE_MOCK_SENSOR_DATA 0
#define HARDWARE_TEST_PHASE 3
```

With hardware-test mode enabled, `app_main()` starts the selected test and
returns before normal application initialization.

See [`hardware_tests/README_test.md`](hardware_tests/README_test.md) for selection,
wiring, commands, expected logs, and explicit PASS/FAIL criteria. See
[`docs/test_plan.md`](docs/test_plan.md) for the complete validation matrix.

## Current scope status

| Area | Status |
|---|---|
| ESP32-S3 target and Phase 1-6 sources | IMPLEMENTED |
| Current ESP32-S3 compilation | Recorded in the Phase 1-6 report after each verified build |
| MPU6050 physical I2C communication | FAILED in the last recorded board run; no slave ACK |
| GPS hardware validation | BLOCKED pending a current board run; parser host tests pass |
| MQ-3 ADC mapping and calibrated conversion | IMPLEMENTED and BUILD VERIFIED; hardware validation remains BLOCKED |
| MPU6050 +/-4 g configuration and scaling | IMPLEMENTED and BUILD VERIFIED; physical I2C remains FAILED in the last recorded board run |
| Accident detector hardware validation | BLOCKED by MPU6050 hardware communication |
| Phase 6 FSM runtime validation | Requires Phase 6 firmware to be flashed and its final summary observed |
| Bike Node project boundary | IMPLEMENTED in `../bike_node/`; hardware runtime remains BLOCKED |
| BLE, Wi-Fi and cloud | Source implemented and build verified; physical runtime/server tests BLOCKED |
| Backend and production power management | Not implemented here |

## Build

Use the installed ESP-IDF v5.5.4 environment:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command `
  "& 'C:\Espressif\tools\Microsoft.v5.5.4.PowerShell_profile.ps1'; idf.py build"
```

A successful command establishes `BUILD VERIFIED` only. Flashing the correct
board and observing the phase-specific serial criteria is required for
`HARDWARE PASS`.

## Development goals

- Build a practical ESP32-S3 embedded IoT prototype.
- Develop modular firmware using C, ESP-IDF, and FreeRTOS.
- Integrate UART, I2C, ADC, Wi-Fi, and BLE in bounded phases.
- Develop a Node.js backend for alert handling and GPS forwarding.
- Demonstrate hardware-firmware-software integration with explicit safety and
  validation criteria.

## Authors

- Nguyen Tien Quan
- Le Huu Tho

Computer Engineering students, Industrial University of Ho Chi Minh City.

GitHub: [github.com/TienQuanNguyen](https://github.com/TienQuanNguyen)
