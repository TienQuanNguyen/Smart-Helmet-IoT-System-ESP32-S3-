# Bike Node ESP32-C3

Bike Node is a separate ESP-IDF project for the ESP32-C3 SuperMini. It is kept
independent from the sibling `../helmet_node/` project.

## Current scope

Phase 10 adds a real NimBLE Central: scan by shared service UUID, discover status
and CCCD, subscribe, validate packets, reconnect with backoff and revoke stale
permission after 3 seconds. The application owns `bike_state` and the relay;
BLE callbacks only update a protected snapshot. Relay updates run every 20 ms
and only write GPIO when the requested state changes.

RC522 reading, the real UID allowlist and OLED rendering are still incomplete.
Authorization therefore stays fail-closed; no fake UID has been added.

The relay GPIO is driven to the inactive level at boot because the approved
relay module is active-high.

## Project structure

```text
bike_node/
├── main/
├── components/
│   ├── ble_comm/
│   ├── bike_state/
│   ├── relay_driver/
│   └── rfid_reader/
└── hardware_tests/
```

## Approved pin map

| Signal | GPIO | Notes |
|---|---:|---|
| Relay control | 1 | Active high; inactive LOW at boot |
| OLED SDA | 9 | OLED is required |
| OLED SCL | 8 | OLED is required |
| OLED address | 0x3C | I2C address |
| RC522 CS | 7 | SPI chip select |
| RC522 SCK | 4 | SPI clock |
| RC522 MISO | 5 | SPI MISO |
| RC522 MOSI | 6 | SPI MOSI |
| RC522 RST | 10 | RC522 reset |

Power notes:

- Relay contact side uses 5 V on COM/BAT as the simulated load side.
- Relay module VCC/control side is 3.3 V.
- RC522 uses 3.3 V logic and supply.

## Build

From this directory:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "& 'C:\Espressif\tools\Microsoft.v5.5.4.PowerShell_profile.ps1'; idf.py build"
```

## BIKE-001 validation

Build success is only `BUILD VERIFIED`. Hardware remains `BLOCKED` until an
ESP32-C3 board is flashed and serial logs show stable boot and repeated
heartbeat output. Do not connect this to a real vehicle ignition circuit.

See [Phase 8–11 verification](../helmet_node/docs/phase_8_to_11_validation.md).
