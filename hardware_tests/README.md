# Hardware tests

This ESP-IDF component contains isolated tests for Phase 1 through Phase 6.
Only one test runs per firmware build, so an unconnected module cannot block an
unrelated test. Phase 20 is an auxiliary I2C diagnostic and is not a project
phase.

## Evidence labels

- `BUILD VERIFIED`: the selected firmware compiled successfully.
- `MOCK VERIFIED`: a named mock scenario ran successfully.
- `HARDWARE PASS`: the physical test met every PASS criterion below.
- `FAILED`: at least one criterion failed.
- `BLOCKED`: the test could not be run because required hardware/configuration
  was unavailable.

Build success is never sufficient for `HARDWARE PASS`.

## Select a test

Edit `main/system_config.h`:

```c
#define HARDWARE_TEST_MODE 1
#define HARDWARE_TEST_PHASE 2
```

| Phase | Test |
|---|---|
| 1 | ESP32-S3 boot, flash, heap, and heartbeat |
| 2 | MPU6050 scan, WHO_AM_I, GPIO7 DATA_READY, and motion samples |
| 3 | NEO-6M parser preflight, raw NMEA, and GGA/GNGGA parsing |
| 4 | MQ-3 ADC mapping preflight, warm-up, average, and threshold output |
| 5 | Deterministic detector preflight plus real MPU6050 impact/fall input |
| 6 | Event queue, real queue-timeout path, and FSM transitions |
| 20 | Auxiliary SSD1306 128x64 I2C test |

Set `HARDWARE_TEST_MODE` to `0` only when intentionally running the normal
application.

## Build and run

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command `
  "& 'C:\Espressif\tools\Microsoft.v5.5.4.PowerShell_profile.ps1'; idf.py build"
```

After verifying the actual ESP32-S3 serial port:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command `
  "& 'C:\Espressif\tools\Microsoft.v5.5.4.PowerShell_profile.ps1'; idf.py -p COMx flash monitor"
```

Exit the monitor with `Ctrl+]`. Never flash an arbitrary available port.

## Phase criteria summary

### Phase 1

PASS requires successful flash-size detection and at least three consecutive
five-second heartbeat logs without panic, reboot, stopped heartbeat, or
continuously decreasing heap.

### Phase 2

Wiring: VCC 3.3 V, GND, SDA GPIO8, SCL GPIO9, INT GPIO7, AD0 GND for 0x68
or 3.3 V for 0x69. Never pull I2C above 3.3 V.

PASS requires idle HIGH/HIGH, ACK at 0x68/0x69, WHO_AM_I 0x68, exact readback
of the 125 Hz / +/-4 g / +/-250 dps configuration, continuous DATA_READY
interrupts, a plausible stationary 1 g result, and at least three plausible
motion logs. No ACK, wrong WHO_AM_I/readback, incorrect 8192 LSB/g scaling,
stuck bus, interrupt timeout, or read error is FAIL.

### Phase 3

Wiring: GPS TXD to GPIO17, GPS RXD to GPIO18, common ground, appropriate module
supply, 9600 baud.

PASS requires all deterministic parser vectors, continuous complete NMEA,
parsed GGA/GNGGA, and a valid outdoor fix. Invalid checksum acceptance, no NMEA
for 30 seconds, UART errors, or invalid coordinates are FAIL.

CHANGE-013 adds the invalid-checksum test but does not fix the parser. The
preflight is expected to report FAILED until CHANGE-003 is approved.

### Phase 4

Wiring: divided AO to GPIO4, external MOSFET/load-switch enable to GPIO5,
common ground. DO is unused. The heater must not be powered from GPIO5. Measure
the GPIO4 node at or below 3.3 V before connection.

PASS requires GPIO4 to map to ADC1 channel 3, successful ESP-IDF curve-fitting
calibration, monotonic raw 0/mid/full-scale conversions, the full 60-second
warm-up, and at least ten valid averaged samples. Mapping mismatch, unavailable
calibration, unsafe voltage, init/read error, or stuck rail data is FAIL.
Logged voltage is the GPIO4 node after the divider. The unchanged 1.80 V value
is not a completed alcohol-concentration calibration.

### Phase 5

Phase 2 must pass first. Use only safe controlled motion.

PASS requires the deterministic normal/impact/fall/repeat preflight, successful
real MPU initialization, stable normal readings, controlled impact detection,
and fall detection only after sustained tilt. Sensor errors, false continuous
alarms, or missed controlled cases are FAIL.

### Phase 6

No sensor wiring is required.

PASS requires every queue/FSM assertion, including the actual empty-queue
timeout checks, and a final `fail=0` summary. Any queue error, unexpected state,
or non-zero fail count is FAIL.

### Auxiliary test 20

Wiring: VCC 3.3 V, GND, SDA GPIO8, SCL GPIO9.

PASS requires ACK at 0x3C/0x3D, successful initialization/framebuffer transfer,
and visible `I2C OK` plus border. Transfer success without visual confirmation
is not a complete hardware pass.

Full step-by-step criteria and result-record requirements are in
`docs/test_plan.md`.
