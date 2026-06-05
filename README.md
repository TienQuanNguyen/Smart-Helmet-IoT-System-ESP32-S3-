# Smart Helmet IoT System – ESP32-S3

## Overview

**Smart Helmet IoT System** is an embedded IoT graduation project developed with **ESP32-S3**, focusing on rider safety, sensor integration, wireless communication, and real-time embedded firmware design.

The system is designed to detect abnormal motion events, check alcohol level before vehicle startup, track GPS location, and send warning data through Wi-Fi/BLE communication.

> This project uses a simulated vehicle start-lock circuit for academic testing only. It does not interfere with any real motorcycle ECU, smart key, or starter system.

---

## Key Technical Highlights

* **Main MCU:** ESP32-S3 with ESP-IDF and FreeRTOS.
* **Firmware Design:** Modular and event-driven architecture.
* **Sensor Integration:** MPU6050, MQ-3 alcohol sensor, and GPS module.
* **Communication:** Wi-Fi as the main channel and BLE as a fallback/local channel.
* **Backend Software:** Node.js server for receiving device data, processing alerts, and sending location information to relatives.
* **Safety Logic:** Accident detection, alcohol checking, GPS-based alert.
* **Bike Node Simulation:** ESP32-C3 controls a relay/transistor circuit for simulated start-lock behavior.
* **Power Management:** MQ-3 sensor is controlled by MOSFET to reduce power consumption.

---

## System Architecture

```text
Helmet Node (ESP32-S3)
│
├── MPU6050          → Motion / fall detection
├── MQ-3             → Alcohol level checking
├── GPS Module       → Location tracking
├── Wi-Fi / BLE      → Alert and data communication
└── Battery System   → Portable power source

Bike Node (ESP32-C3)
│
└── Relay / Transistor Circuit
    └── Simulated 
```

---

## Hardware Components

| Component          | Purpose                      |
| ------------------ | ---------------------------- |
| ESP32-S3 DevKit    | Main helmet controller       |
| ESP32-C3 DevKit    | Simulated bike node          |
| MPU6050            | Motion and fall detection    |
| MQ-3               | Alcohol detection            |
| GPS Module         | Location tracking            |
| Buzzer / LED       | Warning output               |
| Relay / Transistor | Simulated start-lock control |
| MOSFET             | MQ-3 power control           |
| 18650 Battery      | Power supply                 |

---

## Firmware Modules

```text
firmware/
├── sensor_manager      # MPU6050, MQ-3, GPS handling
├── accident_detector   # Motion-based accident detection
├── alcohol_checker     # Pre-start alcohol checking logic
├── comm_manager        # Wi-Fi / BLE communication
├── warning_control     # Buzzer and LED control
├── start_lock_control  # Simulated bike lock signal
└── system_state        # Event and state management
```

---

## Core Logic

### Accident Detection

The system uses MPU6050 acceleration and gyroscope data to detect abnormal motion, impact, or sudden orientation changes.

### Alcohol Checking

The MQ-3 sensor is activated only during the pre-start checking stage to reduce power consumption.
If alcohol level exceeds the threshold, the system triggers a warning and sends a lock signal to the simulated bike node.

### GPS Alert Notification

When an accident is detected, the ESP32-S3 collects GPS data and sends an emergency payload to the app.
The backend processes the event and forwards the alert with location information to relatives.
### Simulated Start-Lock

This mechanism is implemented only on a controlled simulation circuit.

---

## Development Goals

* Build a practical ESP32-S3 embedded IoT prototype.
* Practice firmware development using C, ESP-IDF, and FreeRTOS.
* Design modular firmware for easier testing and maintenance.
* Integrate multiple peripherals using UART, I2C, ADC, Wi-Fi, and BLE.
* Develop a Node.js backend for alert handling and GPS location forwarding.
* Apply power management for high-consumption sensors.
* Demonstrate hardware–firmware–software integration in a safety-oriented system.

---

## Current Status

* [x] System architecture design
* [x] Hardware component selection
* [x] Helmet Node and Bike Node concept
* [ ] ESP32-S3 firmware implementation
* [ ] Sensor driver integration
* [ ] Wi-Fi/BLE communication
* [ ] Start-lock simulation circuit
* [ ] Node.js backend implementation
* [ ] GPS alert notification
* [ ] Final testing and documentation

---

## Tech Stack

* **Language:** C
* **Framework:** ESP-IDF
* **RTOS:** FreeRTOS
* **MCU:** ESP32-S3, ESP32-C3
* **Protocols:** UART, I2C, ADC, Wi-Fi, BLE
* **Tools:** Git/GitHub, ESP-IDF, Serial Monitor

---

## Author

**Nguyen Tien Quan**
**Le Huu Tho**
Computer Engineering Student
Industrial University of Ho Chi Minh City

GitHub: [github.com/TienQuanNguyen](https://github.com/TienQuanNguyen)
