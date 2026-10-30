# Power Budget and Validation Status

Status: Power Manager IMPLEMENTED / host MOCK VERIFIED; measured current-consumption
and real battery sensing remain BLOCKED. See [Phase 12](phase_power_manager.md).

This document intentionally contains no estimated PASS values. Power claims
must be based on measurements from the assembled hardware.

## 1. Power architecture boundary

The project uses a two-cell 18650 Battery Shield with integrated battery
holders, charging and power conversion. This project does not design or add a
separate TP4056, CN3065, LM2596 or custom BMS circuit.

GPIO5 is only the control input for an external MQ-3 MOSFET/load switch. It must
not directly power the MQ-3 heater.

## 2. Current firmware status

| Function | Status | Evidence |
|---|---|---|
| MQ-3 power-enable and calibrated ADC API | IMPLEMENTED | GPIO control, GPIO4 mapping validation and ADC curve-fitting conversion exist in the driver |
| MQ-3 application power schedule | IMPLEMENTED | Existing normal task warms, samples then turns heater off |
| Wi-Fi on-demand policy | SOURCE PRESENT / RUNTIME BLOCKED | `wifi_manager` exists for the phone-hotspot gateway, but credentials and board logs are absent |
| BLE power policy | PRESERVED | Existing NimBLE/Bike protocol and cadence unchanged by Power Manager |
| GPS scheduling policy | PRESERVED | Existing GPS service cadence unchanged |
| Power monitor and estimates | IMPLEMENTED / MOCK VERIFIED | Disabled by default; explicit Mock; real adapter/calibration not configured |
| Low-battery cloud cadence | IMPLEMENTED / MOCK VERIFIED | Routine telemetry interval x3 for valid external LOW/CRITICAL; no emergency delay policy |
| ESP-IDF dynamic power management | BLOCKED | `CONFIG_PM_ENABLE` is disabled |
| Light-sleep validation | BLOCKED | No approved sleep-state/wakeup test exists |
| Deep Sleep while driving | FAILED DESIGN CRITERION | It can miss accident and SOS events and must not be used |

## 3. Required measurements

Record measurements for the same board, supply and firmware build:

| State | Required module state | Current (mA) | Result |
|---|---|---:|---|
| Boot | Initial initialization | NOT MEASURED | BLOCKED |
| Startup alcohol check | MQ-3 heater on | NOT MEASURED | BLOCKED |
| Ready to ride | Required local/BLE functions only | NOT MEASURED | BLOCKED |
| Driving monitoring | MPU6050 monitoring active | NOT MEASURED | BLOCKED |
| GPS acquisition | GPS receiving/fix search | NOT MEASURED | BLOCKED |
| Emergency reporting | GPS plus Wi-Fi/cloud transmission | NOT MEASURED | BLOCKED |
| Low-power idle | Only approved wake sources active | NOT MEASURED | BLOCKED |

For every measurement, record:

- Battery Shield output voltage.
- ESP32-S3 board revision.
- Connected modules.
- Firmware commit or dirty-worktree description.
- Test phase or FSM state.
- Average and peak current.
- Instrument and sample interval.

## 4. PASS/FAIL rule for future power work

`HARDWARE PASS` requires all of the following:

- Measured values are recorded with the conditions above.
- SOS wake behavior is demonstrated for any sleep state.
- The system does not sleep while accident monitoring is required.
- GPS, BLE and Wi-Fi recover to their expected state after wake.
- No module exceeds its electrical limits.

Any unmeasured claim remains `BLOCKED`. A build alone cannot verify power
consumption.
