# Phase 1-6 Verification Plan

Status date: 2026-10-02

## 1. Evidence policy

Use these labels independently:

- `IMPLEMENTED`: relevant source exists and was inspected.
- `BUILD VERIFIED`: current source compiled successfully for ESP32-S3.
- `MOCK VERIFIED`: the named mock scenario was executed successfully.
- `HARDWARE PASS`: the exact physical setup was run and every PASS criterion
  below was observed.
- `FAILED`: at least one explicit criterion was not met.
- `BLOCKED`: the required board, module, port, configuration or external
  contract was not available.

Do not infer `HARDWARE PASS` from a successful build or from mock output.

## 2. Common procedure

Select exactly one test in `main/system_config.h`:

```c
#define HARDWARE_TEST_MODE 1
#define HARDWARE_TEST_PHASE <1..6 or 20>
```

Build:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command `
  "& 'C:\Espressif\tools\Microsoft.v5.5.4.PowerShell_profile.ps1'; idf.py build"
```

Flash only a serial port verified to belong to the ESP32-S3:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command `
  "& 'C:\Espressif\tools\Microsoft.v5.5.4.PowerShell_profile.ps1'; idf.py -p COMx flash monitor"
```

Exit the monitor with `Ctrl+]`. Do not substitute an arbitrary COM port when
the expected board port is unavailable.

## 3. Phase 1 - ESP32-S3 board bring-up

**Setup:** ESP32-S3 board only; no sensor is required.

**PASS criteria**

- Firmware boots without reset loop or panic.
- Log identifies the ESP32-S3 test.
- Flash size is read successfully.
- A `PASS heartbeat` appears every five seconds for at least three consecutive
  intervals.
- Free heap remains non-zero and does not continuously decrease across those
  intervals.

**FAIL criteria**

- Flash-size query fails.
- Status task cannot be created.
- Panic, watchdog reset or unexpected reboot occurs.
- Heartbeat stops or heap continuously falls during the observation window.

## 4. Phase 2 - MPU6050 I2C and DATA_READY

**Wiring**

| MPU6050 | ESP32-S3 |
|---|---|
| VCC | 3.3 V |
| GND | GND |
| SDA | GPIO8 |
| SCL | GPIO9 |
| INT | GPIO7 |
| AD0 | GND for address 0x68; 3.3 V for 0x69 |

I2C pull-ups must not exceed 3.3 V.

**PASS criteria**

- Idle log reports SDA=1 and SCL=1.
- The module ACKs at 0x68 or 0x69.
- WHO_AM_I register 0x75 reads 0x68.
- CONFIG, SMPLRT_DIV, GYRO_CONFIG and ACCEL_CONFIG read back exactly as written.
- ACCEL_CONFIG reads `0x08` for +/-4 g and conversion uses 8192 LSB/g.
- GPIO7 receives DATA_READY interrupts without repeated two-second timeouts.
- At least three periodic `PASS #...` motion logs contain plausible changing
  accel/gyro samples.
- A stationary orientation has total acceleration close to 1 g within the
  module's practical tolerance.
- A controlled motion remains measurable below +/-4 g; deliberate clipping at
  approximately the range limit is recorded as saturation, not a larger value.

**FAIL criteria**

- Either bus line remains low at idle.
- Neither 0x68 nor 0x69 ACKs.
- WHO_AM_I is not 0x68.
- Any configuration-register readback differs from its expected value.
- Static magnitude is implausible or scaling behaves as 16384 LSB/g while the
  register selects +/-4 g.
- I2C returns timeout/bus errors.
- No DATA_READY interrupt is received.
- Motion reads repeatedly fail or remain implausibly fixed.

The last recorded board evidence was `FAILED`: no I2C slave ACKed. That result
must not be relabeled as a driver or sensor `HARDWARE PASS` after a build.

## 5. Phase 3 - NEO-6M UART and GGA

**Wiring**

| NEO-6M | ESP32-S3 |
|---|---|
| TXD | GPIO17 (ESP RX) |
| RXD | GPIO18 (ESP TX, optional for receive-only use) |
| GND | GND |
| VCC | Supply appropriate for the specific breakout |

UART setting: 9600 baud, 8 data bits, no parity, 1 stop bit.

**Deterministic parser preflight PASS criteria**

- Valid GPGGA is accepted.
- Valid GNGGA is accepted.
- Non-GGA sentence is rejected.
- A GGA with an invalid checksum is rejected.
- A no-fix GGA parses but reports `fix_valid=false`.

**Live hardware PASS criteria**

- Complete NMEA sentences are received continuously without UART read errors.
- At least one GGA/GNGGA sentence is parsed.
- Outdoors or with adequate sky view, a valid fix is reported with non-zero
  satellites and coordinates inside legal ranges.

**FAIL criteria**

- Parser preflight reports any failed vector.
- No complete NMEA sentence is received for 30 seconds.
- Repeated malformed/truncated sentences or UART errors occur.
- Invalid checksum data is accepted.
- A claimed valid fix has invalid coordinates or satellite data.

The checksum defect is intentionally not fixed by CHANGE-013. The new preflight
is expected to report `FAILED` until CHANGE-003 is separately approved.

## 6. Phase 4 - MQ-3 power, ADC and sampling

**Wiring and electrical limits**

- MQ-3 AO reaches GPIO4 through the approved external voltage divider.
- GPIO5 controls only an external MOSFET/load-switch enable.
- The heater is powered from a suitable supply, not from GPIO5.
- Grounds are common.
- MQ-3 DO is unused.
- Measure the GPIO4 node at or below 3.3 V before connecting it to the ESP32-S3.

**Deterministic mapping/calibration preflight PASS criteria**

- GPIO4 resolves to ADC1 channel 3.
- The MQ-3 driver configuration selects the same ADC unit and channel.
- ESP-IDF curve-fitting calibration initializes; lack of calibration support
  fails initialization instead of silently using an assumed 3.3 V reference.
- Calibrated conversions for raw 0, 2048 and 4095 are monotonic.

**Live hardware PASS criteria**

- Mapping preflight passes.
- Driver initializes without ADC/GPIO errors.
- GPIO5 enables the external switch as intended.
- The full configured 60-second warm-up completes.
- At least ten consecutive averaged samples are logged without read errors.
- Stable normal, deliberately noisy and controlled above-threshold samples are
  distinguishable without being stuck at either ADC rail.
- Logged voltage is explicitly the GPIO4 ADC-node voltage after the divider.

**FAIL criteria**

- Mapping preflight reports a different ADC GPIO.
- Calibration initialization/conversion is unavailable or non-monotonic.
- ADC input exceeds 3.3 V.
- Heater current is sourced from an ESP32 GPIO.
- Initialization, warm-up or ADC reads fail.
- Samples are stuck at rail values or do not respond plausibly.

An `alcohol=0/1` threshold result is not by itself a hardware PASS and is not a
calibrated breath-alcohol result. CHANGE-001 corrected the ADC mapping and ADC
conversion, but the unchanged 1.80 V decision threshold still requires
experimental calibration.

## 7. Phase 5 - Accident detector with real MPU6050

**Setup:** Phase 2 wiring must already pass. Use controlled hand motion; do not
drop the assembled unit or perform a dangerous real crash test.

**Deterministic detector preflight PASS criteria**

- Upright 1 g input remains normal.
- An input above the configured magnitude threshold reports impact.
- Tilt above the configured angle for the confirmation duration reports fall.
- After explicit reset, a second controlled incident can be detected.

**Live hardware PASS criteria**

- MPU6050 initializes successfully.
- Normal stationary and gentle movement do not continuously report impact/fall.
- A controlled threshold-crossing motion reports impact.
- A controlled sustained tilt reports fall only after the configured duration.
- Sensor/detector updates continue without repeated errors.

**FAIL criteria**

- Any deterministic preflight vector fails.
- MPU6050 initialization/read fails.
- Normal stationary input repeatedly triggers an incident.
- Controlled impact/tilt never triggers the corresponding result.

The application-level one-event-per-boot latch remains a known issue for
CHANGE-004. CHANGE-013 does not repair that production behavior.

## 8. Phase 6 - Event queue and FSM

No sensor wiring is required.

**PASS criteria**

- Queue publish/receive preserves each test event.
- Initial state is `STARTUP_ALCOHOL_CHECK`.
- Alcohol pass enters `READY_TO_RIDE`.
- A real empty-queue timeout returns no event and the tested timeout path enters
  `DRIVING_MONITORING`.
- Impact enters `ACCIDENT_DETECTED`.
- A subsequent real empty-queue timeout enters `EMERGENCY_REPORTING`.
- Alcohol fail enters `LOW_POWER_IDLE`.
- SOS enters `EMERGENCY_REPORTING`.
- Battery low enters `LOW_POWER_IDLE` outside emergency.
- Final summary reports `fail=0`.

**FAIL criteria**

- Queue publish/receive or an expected timeout behaves incorrectly.
- Any expected state differs from the actual state.
- Final summary reports one or more failures.

This phase verifies the current baseline FSM only. It does not add the proposed
emergency cancellation state from CHANGE-006.

## 9. Auxiliary test 20 - SSD1306 I2C path

This is not a numbered project phase.

**Wiring:** VCC 3.3 V, GND, SDA GPIO8, SCL GPIO9.

**PASS criteria**

- SDA/SCL are idle high.
- SSD1306 ACKs at 0x3C or 0x3D.
- Initialization and framebuffer transfer complete without I2C error.
- The display visibly shows a border and `I2C OK`.

**FAIL criteria**

- No ACK, bus timeout, initialization/write error, or incorrect/missing visual
  output.

A successful framebuffer write without visual confirmation is not a complete
`HARDWARE PASS`.

## 10. Result record

For each run, record:

- Date and operator.
- Git commit or dirty-worktree description.
- ESP-IDF version and target.
- Selected phase.
- Board and module identifiers.
- Verified serial port.
- Wiring and supply voltage.
- Relevant serial log excerpt.
- Final label: `HARDWARE PASS`, `FAILED`, or `BLOCKED`.
