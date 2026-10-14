# Smart Helmet IoT System - Proposed Changes

Date: 2026-10-02

Status: CHANGE-001, CHANGE-002 AND CHANGE-013 APPROVED AND IMPLEMENTED; ALL OTHER CHANGE IDS WAITING FOR USER APPROVAL

Framework: ESP-IDF v5.x  
Primary language: C  
Helmet MCU: ESP32-S3  
Bike MCU: ESP32-C3

## 1. Scope and approval rule

This document records the proposed changes identified from the current project
audit and the supplied reference requirements.

The entries below are plans only. Creating this document does not approve any
firmware change.

Implementation may begin only after the user explicitly approves one or more
IDs, for example:

```text
APPROVE CHANGE-001
APPROVE CHANGE-003
```

Unapproved changes must not be implemented.

The following project constraints remain in force:

- Keep ESP-IDF and C; do not migrate to Arduino or C++ architecture.
- Preserve the current pin map unless physical diagnostics justify a change.
- Preserve MPU6050 I2C GPIO8/GPIO9, address handling and WHO_AM_I behavior.
- Do not change the MQ-3 alcohol threshold without experimental calibration.
- Do not add MQTT, Machine Learning or a new cloud service without approval.
- Do not design a separate charging/BMS circuit. The project uses a two-cell
  18650 Battery Shield.
- Do not use Deep Sleep while accident monitoring is required.
- Reference code is used for techniques only; it does not replace the current
  project architecture.

## 2. Priority definitions

| Priority | Meaning |
|---|---|
| A - HIGH VALUE | Corrects a concrete defect or fills a core functional gap |
| B - USEFUL | Improves robustness or architecture but is not the first blocker |
| C - OPTIONAL | Enhancement to consider after core functionality is stable |
| D - DO NOT USE | Incompatible, unnecessary or insufficiently justified |

## 3. Proposed changes

### CHANGE-001 - Correct MQ-3 ADC mapping and calibrated voltage

**Approval:** APPROVED by user on 2026-10-02  
**Implementation status:** IMPLEMENTED and `BUILD VERIFIED`. GPIO4 is validated
as ADC1 channel 3 and raw conversion uses ESP-IDF curve-fitting calibration.
Hardware execution remains `BLOCKED` pending an identifiable ESP32-S3 serial
port and safe measured ADC-node voltage.

**Current at audit time**

- `MQ3_DEFAULT_ADC_UNIT` is stored as the integer `1` and cast to
  `adc_unit_t`.
- ESP-IDF defines `ADC_UNIT_1` as the first enum value. The current value can
  therefore select ADC2 rather than ADC1.
- On ESP32-S3, ADC1 channel 3 corresponds to GPIO4, while ADC2 channel 3 is a
  different GPIO.
- `PIN_MQ3_ADC` is declared but is not used to validate or select the ADC
  channel.
- Voltage is calculated as `raw / 4095 * 3.3`, without ESP-IDF ADC
  calibration.

**Reference**

- ESP-IDF ADC Oneshot API.
- ESP-IDF ADC calibration API and `oneshot_read` example.

**Proposed**

- Use typed `ADC_UNIT_1` and `ADC_CHANNEL_3`, or derive the unit/channel from
  `PIN_MQ3_ADC` using `adc_oneshot_io_to_channel()`.
- Validate at initialization that GPIO4 maps to the configured ADC unit and
  channel.
- Add an ADC calibration handle and convert raw data to calibrated millivolts.
- Clearly distinguish voltage at the ESP32 ADC node from the original MQ-3 AO
  voltage before the external divider.
- Preserve the current 1.80 V threshold until measured calibration data is
  available.

**Files affected**

- `components/mq3_driver/include/mq3_driver.h`
- `components/mq3_driver/mq3_driver.c`
- `hardware_tests/phase4_mq3_test.c`
- `main/system_config.h`
- `hardware_tests/README.md`
- Related technical documentation

**New files:** NONE  
**External dependencies:** NONE; use the existing ESP-IDF `esp_adc` component  
**API impact:** Type-safe ADC configuration and clarified voltage semantics  
**Risk:** MEDIUM  
**Expected benefit:** Ensures the firmware reads GPIO4 through the intended ADC
and produces a meaningful voltage value  
**Priority:** A - HIGH VALUE

**Required tests**

- Confirm GPIO4 resolves to ADC1 channel 3.
- ADC zero, mid-scale and high-scale input tests.
- Calibration-supported and calibration-unavailable paths.
- Stable normal samples, noisy samples and above-threshold samples.
- Verify the physical ADC node is at or below 3.3 V before connection.
- Verify the full 60-second configured warm-up path.

---

### CHANGE-002 - Align MPU6050 measurement range with the impact threshold

**Approval:** APPROVED by user on 2026-10-02  
**Implementation status:** IMPLEMENTED and `BUILD VERIFIED`. The default range
is +/-4 g with 8192 LSB/g, configurable gyro/DLPF/sample divider fields and
register readback. Current hardware revalidation is `BLOCKED`; the last
recorded board attempt remains `FAILED` because neither 0x68 nor 0x69 ACKed.

**Current at audit time**

- The production driver configures the accelerometer for +/-2 g and converts
  with 16384 LSB/g.
- The accident detector threshold is 3.0 g.
- A single-axis impact near or above 3 g cannot be measured faithfully in the
  configured +/-2 g range.
- The isolated Phase 2 hardware test uses +/-4 g, so the test configuration and
  production driver are inconsistent.

**Reference**

- MPU6050 register definitions.
- `esp-idf-lib/mpu6050` configurable range, DLPF and sample-rate techniques.

**Proposed**

- Configure the production path for at least +/-4 g and use the matching
  8192 LSB/g scale.
- Add explicit accelerometer range, gyroscope range, DLPF and sample-divider
  fields to `mpu6050_config_t` if approved.
- Keep GPIO8/GPIO9, I2C address behavior and WHO_AM_I expectations unchanged.
- Do not migrate the legacy I2C API as part of this change.

**Files affected**

- `components/mpu6050_driver/include/mpu6050_driver.h`
- `components/mpu6050_driver/mpu6050_driver.c`
- `main/main.c`
- `hardware_tests/phase2_mpu6050_test.c`
- `hardware_tests/phase5_accident_test.c`
- Related documentation

**New files:** NONE  
**External dependencies:** NONE  
**API impact:** Extend `mpu6050_config_t`  
**Risk:** MEDIUM; range and scale directly affect accident calculations  
**Expected benefit:** Prevents impact clipping and aligns Phase 2, Phase 5 and
normal firmware behavior  
**Priority:** A - HIGH VALUE

**Required tests**

- WHO_AM_I and configuration register readback.
- Static orientation close to 1 g.
- Controlled motion within +/-4 g.
- Controlled saturation test.
- Verify converted values use the selected range scale.
- Confirm no GPIO, address or WHO_AM_I change.

---

### CHANGE-003 - Harden GPS UART framing and GGA parsing

**Current**

- The parser accepts GPGGA and GNGGA but does not validate NMEA checksum.
- The checksum suffix is removed without being checked.
- A partial line can be returned when a read timeout occurs.
- `gps_read_data()` examines only one received sentence; a valid GGA following
  an RMC or vendor sentence is not reached in the same call.
- Numeric fields, hemisphere fields and coordinate ranges receive limited
  validation.
- The current mock sentence contains a checksum that does not match its
  payload, but the test still passes because checksum validation is absent.

**Reference**

- ESP-IDF `peripherals/uart/nmea0183_parser` example.

**Proposed**

- Resynchronize at `$` and accept only complete newline-terminated sentences.
- Discard overlong or truncated frames safely.
- Calculate and validate the NMEA XOR checksum in `*HH` format.
- Continue reading sentences until a valid GGA/GNGGA is found or the overall
  timeout expires.
- Validate fix quality, numeric parsing, hemisphere and latitude/longitude
  ranges.
- Correct the mock NMEA checksum.
- Keep the existing `gps_driver`; do not create a second GPS/parser module.

**Files affected**

- `components/gps_driver/include/gps_driver.h`
- `components/gps_driver/gps_driver.c`
- `hardware_tests/phase3_gps_test.c`
- `main/app_test.c`

**New files:** NONE  
**External dependencies:** NONE  
**API impact:** Existing public API can remain; success criteria become stricter  
**Risk:** LOW to MEDIUM  
**Expected benefit:** Prevents corrupted, partial or unrelated NMEA data from
being treated as a valid GPS result  
**Priority:** A - HIGH VALUE

**Required tests**

- Valid GPGGA and GNGGA.
- Correct and incorrect checksums.
- Missing checksum according to the approved policy.
- Partial, overlong and noise-prefixed lines.
- Non-GGA sentence followed by valid GGA.
- No-fix GGA.
- Invalid hemisphere and out-of-range coordinates.
- UART timeout without a complete sentence.

---

### CHANGE-004 - Re-arm and stabilize accident detection

**Current**

- `impact_event_sent` and `fall_event_sent` are set once and never re-armed.
- Only one impact and one fall event can therefore be published during a boot.
- The caller advances a nominal timestamp by a fixed sample period instead of
  using actual monotonic elapsed time.
- Impact is a single threshold comparison and has no hysteresis or cooldown.

**Reference**

- Temporal window and hysteresis techniques.
- MPU6050 sample-rate and motion configuration concepts.

**Proposed**

- Use a monotonic runtime timestamp.
- Trigger impact on a defined threshold crossing.
- Add hysteresis, cooldown and explicit re-arm conditions.
- Preserve the current baseline rule that impact or confirmed fall may trigger
  the accident flow.
- Do not add Machine Learning, Kalman filtering or adaptive thresholds.
- Consider gyro/freefall only after recorded hardware data demonstrates a
  concrete benefit.

**Files affected**

- `components/accident_detector/include/accident_detector.h`
- `components/accident_detector/accident_detector.c`
- `main/main.c` or the proposed application controller
- `main/system_config.h`
- `hardware_tests/phase5_accident_test.c`

**New files:** NONE  
**External dependencies:** NONE; built-in `esp_timer` may be used  
**API impact:** Detector configuration and re-arm policy  
**Risk:** MEDIUM  
**Expected benefit:** Supports multiple incidents per boot and reduces event
chatter  
**Priority:** A - HIGH VALUE

**Required tests**

- Normal upright and normal road vibration.
- Sudden bump and threshold crossing.
- Sustained tilt.
- Impact without fall and fall without strong impact.
- Genuine impact-plus-fall sequence.
- Cooldown and re-arm.
- A second incident after re-arm.
- Timestamp wraparound behavior.

---

### CHANGE-005 - Implement real startup alcohol and GPS application flows

**Current**

- The normal application initializes MQ-3 but has no real power-on, warm-up,
  sampling or alcohol event sequence.
- The normal application initializes GPS but has no recurring GPS task.
- Real sensor data does not publish `SYSTEM_EVENT_ALCOHOL_PASS`,
  `SYSTEM_EVENT_ALCOHOL_FAIL` or `SYSTEM_EVENT_GPS_VALID`.
- The current successful normal-flow behavior depends on mock events.

**Proposed**

- Add a non-blocking startup alcohol workflow:
  power on -> warm-up -> sample/average -> PASS or FAIL -> power off.
- Add a scheduled GPS task that stores the latest validated fix and publishes
  `SYSTEM_EVENT_GPS_VALID`.
- Keep driver code hardware-focused; place orchestration in the Application
  Layer.
- Replace production use of mock/test mode only after the relevant hardware
  phases pass.

**Files affected**

- `main/main.c`
- `main/CMakeLists.txt`
- `main/system_config.h`
- MQ-3/GPS application integration points

**New files**

- `main/helmet_app.c`
- `main/helmet_app.h`

**External dependencies:** NONE  
**API impact:** Application-internal APIs  
**Risk:** MEDIUM  
**Expected benefit:** Drives the FSM with real sensor data instead of mock-only
events  
**Priority:** A - HIGH VALUE

**Required tests**

- Non-blocking 60-second warm-up.
- Exactly one PASS/FAIL result for each startup attempt.
- MQ-3 power-off after completion.
- GPS valid-fix caching and invalid-fix rejection.
- GPS timeout must not block the system manager.
- Mock and real modes must remain explicitly selectable.

---

### CHANGE-006 - Use explicit FSM timers and add emergency cancellation

**Current**

- `SYSTEM_EVENT_NONE` doubles as a queue-timeout signal.
- `READY_TO_RIDE` enters `DRIVING_MONITORING` on a timeout-dependent handler
  call.
- `ACCIDENT_DETECTED` enters `EMERGENCY_REPORTING` on the next handler call,
  regardless of which event caused it.
- There is no warning/countdown/cancellation state.

**Reference**

- Smart Helmet emergency-flow idea:
  impact -> local warning -> cancellation window -> emergency report.

**Proposed**

- Replace implicit `NONE` timing with an explicit timeout/tick mechanism.
- Add an `EMERGENCY_PENDING` state if approved.
- Add explicit emergency cancel and countdown-expired events.
- Start a buzzer warning during the cancellation window.
- Allow SOS to bypass the cancellation delay when immediate reporting is the
  approved behavior.
- Make the cancellation duration configurable; choose 10 or 15 seconds only
  after user approval.

**Files affected**

- `components/event_manager/include/event_manager.h`
- `components/system_state/include/system_state.h`
- `components/system_state/system_state.c`
- Application system-manager task
- `main/system_config.h`
- `hardware_tests/phase6_fsm_test.c`
- FSM documentation

**New files:** NONE  
**External dependencies:** NONE  
**API impact:** Event and state enum changes  
**Risk:** HIGH  
**Expected benefit:** Clear FSM timing and a user-controlled false-alarm escape
path  
**Priority:** B - USEFUL

**Required tests**

- Real queue timeout behavior.
- Alcohol pass/fail baseline transitions.
- Accident -> emergency pending.
- Cancel within the window.
- Countdown expiration.
- SOS immediate path.
- Battery-low event during pending/reporting states.
- Verify the configured delay added to emergency latency.

---

### CHANGE-007 - Add SOS, buzzer and LED hardware layer

**Current**

- GPIO6, GPIO10 and GPIO2 are defined for SOS, buzzer and status LED.
- No GPIO initialization, debounce, interrupt, output pattern or state mapping
  exists.

**Reference**

- ESP-IDF generic GPIO example and ISR-to-task/queue technique.

**Proposed**

- Create one hardware-only Helmet I/O component.
- Keep the ISR minimal and perform debounce/event publication in task context.
- Publish `SYSTEM_EVENT_SOS_PRESSED` from a confirmed button action.
- Expose LED and buzzer hardware APIs without embedding FSM/business logic.
- Map system states to LED/buzzer patterns in the Application Layer.
- Select GPIO output or LEDC only after confirming whether the buzzer is active
  or passive.

**Files affected**

- `main/CMakeLists.txt`
- Application/event/system integration
- Hardware and architecture documentation

**New files**

- `components/helmet_io/CMakeLists.txt`
- `components/helmet_io/helmet_io.c`
- `components/helmet_io/include/helmet_io.h`

**External dependencies:** ESP-IDF GPIO; LEDC only if required  
**API impact:** New internal driver API  
**Risk:** MEDIUM  
**Expected benefit:** Completes essential local safety input and feedback  
**Priority:** A - HIGH VALUE  
**Blocker:** Confirm active or passive buzzer

**Required tests**

- Button bounce, short press, long press, repeat press and stuck-low input.
- SOS event is published once per confirmed press.
- LED patterns for each relevant system state.
- Buzzer warning/cancel patterns.
- ISR load and queue behavior.

---

### CHANGE-008 - Add event metadata and queue failure visibility

**Current**

- Events are type-only enum values.
- Events contain no timestamp, source or payload.
- Non-blocking queue publish can fail when the queue is full, without a
  centralized diagnostic or drop count.

**Proposed**

- Introduce an event envelope containing type, timestamp, source and a minimal
  bounded payload.
- Define a payload only for events that need data; avoid an unnecessarily large
  protocol.
- Add publish-failure logging and a queue-drop counter.
- Define delivery behavior for emergency and SOS events.
- Keep a single event manager; do not create duplicate callback networks.

**Files affected**

- `components/event_manager/include/event_manager.h`
- `components/event_manager/event_manager.c`
- All event publishers and consumers
- Phase 6 and future communication tests

**New files:** NONE  
**External dependencies:** NONE  
**API impact:** Significant change to publish/wait arguments  
**Risk:** MEDIUM to HIGH  
**Expected benefit:** Observable queue behavior and sufficient context for
BLE/cloud integration  
**Priority:** B - USEFUL

**Required tests**

- Queue-full behavior and drop counter.
- Event ordering.
- Emergency/SOS delivery policy.
- Payload bounds and malformed data rejection.
- Timestamp and source correctness.

---

### CHANGE-009 - Implement a minimal Helmet/Bike BLE contract

**Current**

- `components/ble_comm` is empty.
- Bluetooth is disabled in the current sdkconfig.
- No BLE roles or application protocol are implemented.
- Bike Node source is absent.

**Reference**

- ESP-IDF NimBLE `bleprph` and `blecent` examples.

**Proposed**

- Helmet: BLE Peripheral and GATT Server.
- Bike: BLE Central and GATT Client.
- Define one small versioned status characteristic and one command/control
  characteristic.
- Use notification for Helmet state changes.
- Validate message version, length and allowed values.
- Implement disconnect, reconnect, backoff, supervision timeout and state
  resynchronization.
- Add a BLE-lost event; do not rely on `BLE_CONNECTED` alone.
- Do not copy the reference Alert Notification Service or build an excessive
  application protocol.

**Files affected**

- Helmet main/event/state integration
- Reproducible sdkconfig defaults
- Bike files cannot yet be named because no Bike project exists

**New files**

- `components/ble_comm/CMakeLists.txt`
- `components/ble_comm/ble_comm.c`
- `components/ble_comm/include/ble_comm.h`
- A minimal shared protocol header if the two-node repository layout permits it

**External dependencies:** Built-in ESP-IDF NimBLE  
**API impact:** New BLE API, protocol structs and connection events  
**Risk:** HIGH  
**Expected benefit:** Helmet readiness/emergency synchronization with Bike Node  
**Priority:** B - USEFUL  
**Blocker:** Bike Node repository and shared-code layout are not available

**Required tests**

- Advertising, scan, connect, service discovery and subscribe.
- Notification of state changes.
- Disconnect/reconnect and either node power cycle.
- Malformed length/version/value.
- Duplicate and out-of-order message behavior.
- Supervision timeout and state resynchronization.

---

### CHANGE-010 - Implement Bike RC522 authentication and relay fail-safe

**Current**

- No Bike ESP32-C3 project exists in this repository.
- RC522, authentication and relay behavior are absent.

**Reference**

- `abobija/esp-idf-rc522` for SPI transport, card detection, lifecycle and UID
  events.

**Proposed**

- Create or use a separate ESP32-C3 Bike Node project after its location is
  approved.
- Keep RC522 code responsible only for hardware/card operations.
- Keep UID allowlist and authentication decisions in the Bike application
  layer.
- Relay must default OFF and fail OFF on boot, reset, BLE disconnect, state
  timeout, authentication failure or internal error.
- Require both the approved RFID condition and valid Helmet readiness state
  before enabling the relay.

**Files affected:** NONE in the Helmet project until the Bike project boundary
is approved  
**New files:** Separate Bike Node project files  
**External dependencies:** Proposed `abobija/rc522`, Apache-2.0  
**API impact:** Bike-only APIs and states  
**Risk:** HIGH  
**Expected benefit:** Implements the vehicle lock/authentication requirement  
**Priority:** A - HIGH VALUE, CURRENTLY BLOCKED

**Required tests**

- Valid, invalid and repeated UID.
- Card removal and reader error.
- BLE unavailable/disconnected/stale state.
- Relay remains OFF during boot/reset/error/timeout.
- State synchronization after reconnect.

---

### CHANGE-011 - Add Wi-Fi STA and HTTP emergency transport

**Current**

- The Wi-Fi component directory is empty.
- Neither HTTP nor MQTT is used by application source.
- No backend endpoint, payload schema, certificate strategy or authentication
  contract is present.

**Reference**

- ESP-IDF Wi-Fi station example.
- ESP-IDF `esp_http_client` example and API.

**Proposed**

- Implement a Wi-Fi station manager with event-based connection state,
  reconnect and bounded backoff.
- Translate Wi-Fi events into the project event manager.
- Create a separate HTTPS cloud client task so network operations do not block
  the system manager.
- Send only Smart Helmet emergency data and the latest validated GPS fix.
- Keep credentials out of source control.
- Use HTTP/HTTPS for the initial implementation; do not add MQTT without a new
  requirement.

**Files affected**

- Main/application integration
- Event and state integration
- Reproducible sdkconfig defaults
- Security/deployment documentation

**New files**

- Files under `components/wifi_manager/`
- `components/cloud_client/CMakeLists.txt`
- `components/cloud_client/cloud_client.c`
- `components/cloud_client/include/cloud_client.h`

**External dependencies:** Built-in `esp_wifi`, `esp_netif`, `nvs_flash`,
`esp_http_client` and TLS  
**API impact:** New Wi-Fi/cloud APIs and events  
**Risk:** HIGH  
**Expected benefit:** Implements actual emergency reporting  
**Priority:** B - USEFUL  
**Blocker:** Backend URL, authentication, certificate and payload contract

**Required tests**

- Correct and incorrect Wi-Fi credentials.
- AP loss and reconnect.
- DNS, TLS and request timeout failures.
- HTTP 2xx, 4xx and 5xx handling.
- Retry/backoff bounds and duplicate-report policy.
- Valid, stale and unavailable GPS fix.
- Verify no secret is logged or committed.

---

### CHANGE-012 - Introduce a state-aware power policy

**Current**

- `components/power_manager` is empty.
- ESP-IDF power management is disabled.
- There is no battery monitor or measured power budget.
- MQ-3 power gating exists at driver level but is not integrated into the normal
  application flow.

**Reference**

- ESP-IDF power-management locks, DFS and Light-sleep examples.

**Proposed**

- First implement MQ-3 power gating, Wi-Fi on-demand and GPS scheduling.
- Measure current consumption in each system state before claiming savings.
- Consider DFS and automatic Light-sleep only after core functions pass.
- Permit Light-sleep only in an approved low-power state with explicit SOS
  wake-up behavior.
- Do not enter Deep Sleep while accident monitoring is required.
- Integrate the existing two-cell 18650 Battery Shield only; do not design a
  separate charger or BMS.

**Files affected**

- Application and system-state integration
- `main/system_config.h`
- Reproducible sdkconfig defaults
- `docs/power_budget.md`

**New files**

- Files under `components/power_manager/`

**External dependencies:** Built-in ESP-IDF PM/sleep APIs  
**API impact:** New power policy API/events  
**Risk:** HIGH; an incorrect sleep policy can miss safety events  
**Expected benefit:** Reduced consumption without weakening monitoring  
**Priority:** C - OPTIONAL until the core system works

**Required tests**

- Measured current in every relevant state.
- SOS wake behavior.
- GPS/UART and BLE/Wi-Fi behavior after wake.
- Verify no accident-monitoring state enters unsafe sleep.
- Reconnect and state resynchronization after wake.

---

### CHANGE-013 - Make tests and documentation evidence-based

**Approval:** APPROVED by user on 2026-10-01  
**Implementation status:** IMPLEMENTED; build is `BUILD VERIFIED`; target runtime tests are `BLOCKED` because no ESP32-S3 USB serial port was identifiable.

**Current**

- The Phase 1-6 report labels multiple phases PASS even where only build/mock
  verification exists.
- `docs/architecture.md`, `docs/test_plan.md` and `docs/power_budget.md` are
  empty.
- GPS checksum, MQ-3 ADC mapping, repeated incidents and the actual queue
  timeout path are not adequately tested.

**Proposed**

- Use distinct status labels:
  `IMPLEMENTED`, `BUILD VERIFIED`, `MOCK VERIFIED`, `HARDWARE PASS`, `FAILED`
  and `BLOCKED`.
- Add exact PASS/FAIL criteria to each isolated hardware test.
- Add GPS parser vectors, ADC mapping checks, repeated-incident tests and an
  actual queue-timeout FSM test.
- Populate architecture, test-plan and power-budget documents with verified
  information only.
- Keep build success separate from board/sensor validation.

**Files affected**

- Relevant files under `hardware_tests/`
- `hardware_tests/README.md`
- `hardware_tests/CMakeLists.txt`
- `hardware_tests/include/hardware_test.h`
- `main/main.c` (hardware-test configuration only)
- `README.md`
- `docs/architecture.md`
- `docs/test_plan.md`
- `docs/power_budget.md`
- `docs/phase_1_to_6_implementation_report.md`

**New files:** Host/Unity test files only if separately approved  
**External dependencies:** NONE  
**API impact:** NONE  
**Risk:** LOW  
**Expected benefit:** Prevents unsupported PASS claims and makes validation
repeatable  
**Priority:** A - HIGH VALUE

**Required tests**

- Verify each status in the report has matching build/mock/serial/hardware
  evidence.
- Verify every Phase test documents pins, command, expected logs and PASS/FAIL
  criteria.
- Run targeted tests for every modified module after implementation approval.

---

### CHANGE-014 - Restore correct layer dependencies

**Current**

- GPS, MQ-3 and accident-processing components include headers from `main`
  through private include paths.
- Driver and Processing layers therefore depend on the Application Layer.

**Proposed**

- Keep driver defaults inside each driver.
- Construct explicit configuration structs in the Application Layer and pass
  them into components.
- Pass detector thresholds through a detector config rather than including
  `main/system_config.h` in the Processing Layer.
- Remove `../../main` private include paths after all users are migrated.
- Preserve current public APIs where practical; do not create duplicate
  `*_v2` modules.

**Files affected**

- GPS, MQ-3 and accident-detector headers/sources/CMakeLists
- Main/application initialization
- Architecture documentation

**New files:** NONE beyond the application files proposed by CHANGE-005  
**External dependencies:** NONE  
**API impact:** Configuration structs may be extended  
**Risk:** MEDIUM  
**Expected benefit:** Restores the intended Application -> Processing -> Driver
dependency direction and improves isolated testing  
**Priority:** B - USEFUL

**Required tests**

- Each component builds without a private include path to `main`.
- Default and explicit configuration paths.
- Mock and hardware-test builds.
- Verify no duplicate functionality or component is introduced.

## 4. Explicitly rejected techniques

The following items are not proposed for implementation:

| Technique or dependency | Classification | Reason |
|---|---|---|
| Arduino framework or C++ architecture | D - DO NOT USE | Conflicts with the project framework |
| TinyGPS++ | D - DO NOT USE | Existing ESP-IDF C GPS driver should be improved |
| Arduino MPU6050 libraries | D - DO NOT USE | Would replace the current register-level driver |
| A second GPS/MPU/MQ-3 module | D - DO NOT USE | Duplicate functionality |
| Kalman filter | D - DO NOT USE currently | No demonstrated need for its complexity |
| Adaptive accident threshold | D - DO NOT USE currently | No hardware dataset exists |
| Machine Learning accident detection | D - DO NOT USE | Outside the approved baseline and lacks data |
| MQTT | D - DO NOT USE currently | No requirement or implemented cloud contract |
| Deep Sleep while driving | D - DO NOT USE | May miss accident and SOS events |
| TP4056, CN3065, LM2596 or custom BMS | D - DO NOT USE | Project uses an integrated 18650 Battery Shield |

## 5. API impact summary

### Existing APIs potentially affected

- `mpu6050_init_with_config()`
- `gps_parse_nmea()`
- `gps_read_raw_line()`
- `mq3_init_with_config()`
- `mq3_read_voltage()`
- `accident_detector_update()`
- `event_manager_publish()`
- `event_manager_wait()`
- `system_state_handle_event()`

### New APIs potentially introduced

- `mq3_convert_raw_to_voltage()` was introduced by approved CHANGE-001.
- `mpu6050_config_t` range/filter/sample-divider fields were introduced by
  approved CHANGE-002.
- Application controller start/stop API.
- Helmet I/O initialization and output APIs.
- Configurable accident-detector initialization.
- Event drop/diagnostic API.
- BLE communication and connection-state APIs.
- Wi-Fi manager API.
- Cloud emergency submission API.
- State-aware power-manager API.

### Removed APIs

NONE proposed initially.

### Potential enum and FSM changes

- BLE disconnected/timeout event.
- Explicit ride-start timeout event.
- Emergency cancel and countdown-expired events.
- Optional `SYSTEM_STATE_EMERGENCY_PENDING`.

These enum/FSM changes belong to CHANGE-006, CHANGE-008 and CHANGE-009 and
must not be introduced unless their IDs are approved.

## 6. Dependency impact summary

| Dependency | Decision |
|---|---|
| ESP-IDF ADC calibration | Use; built into ESP-IDF |
| ESP-IDF NimBLE | Use only for approved BLE phase |
| ESP-IDF Wi-Fi/netif/NVS | Use only for approved Wi-Fi phase |
| ESP-IDF HTTP client/TLS | Use only for approved cloud phase |
| ESP-IDF PM/sleep APIs | Optional and deferred |
| `esp-idf-lib/mpu6050` | Do not add; extract techniques only |
| `abobija/rc522` | Proposed only for the future Bike project |
| MQTT component | Do not add currently |
| Arduino/TinyGPS++ | Do not add |

## 7. Proposed implementation order

1. CHANGE-013 - Correct test/report status and establish a trustworthy baseline.
2. CHANGE-001 - Correct MQ-3 ADC mapping and voltage conversion.
3. CHANGE-002 - Align MPU6050 range with the impact threshold.
4. CHANGE-003 - Harden GPS framing and parsing.
5. CHANGE-004 - Fix accident-event re-arm and timing.
6. CHANGE-014 - Restore component dependency direction.
7. CHANGE-005 - Add real MQ-3 and GPS application flows.
8. CHANGE-007 - Add SOS, buzzer and LED integration.
9. CHANGE-008 - Improve event metadata and queue diagnostics.
10. CHANGE-006 - Add explicit FSM timers and cancellation UX.
11. CHANGE-009 - Implement Helmet BLE after defining the Bike boundary.
12. CHANGE-010 - Implement Bike RFID/relay firmware.
13. CHANGE-011 - Implement Wi-Fi and HTTP cloud reporting.
14. CHANGE-012 - Optimize power only after measurement.

Recommended first approval group:

```text
APPROVE CHANGE-013
APPROVE CHANGE-001
APPROVE CHANGE-002
APPROVE CHANGE-003
APPROVE CHANGE-004
```

## 8. Approval state

Only CHANGE-001, CHANGE-002 and CHANGE-013 have been approved and implemented.
No other CHANGE described in this document has been implemented.

Implementation is stopped after CHANGE-002. Every other pending CHANGE requires
a new explicit approval.
