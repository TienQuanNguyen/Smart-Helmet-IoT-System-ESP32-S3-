#pragma once

#define BIKE_NODE_NAME "BIKE_NODE_ESP32C3"
#define BIKE_NODE_BOARD "ESP32-C3 SuperMini"

#define BIKE_HEARTBEAT_PERIOD_MS 5000U

/* Set to 1 to run one isolated Bike hardware test instead of the application.
 */
#define BIKE_HARDWARE_TEST_MODE 0
#define BIKE_HARDWARE_TEST_PHASE 1

/* Relay: active-low module. Inactive HIGH is the safe boot state. */
#define BIKE_RELAY_GPIO 1
#define BIKE_RELAY_ACTIVE_LEVEL 0
#define BIKE_RELAY_INACTIVE_LEVEL 1
#define BIKE_RELAY_CONTACT_SUPPLY_MV 5000
#define BIKE_RELAY_MODULE_VCC_MV 3300

/* OLED: required status display, initialized in a later Bike phase. */
#define BIKE_OLED_REQUIRED 1
#define BIKE_OLED_SDA_GPIO 9
#define BIKE_OLED_SCL_GPIO 8
#define BIKE_OLED_ADDR 0x3C

/* RC522: 3.3 V SPI RFID reader. */
#define BIKE_RFID_SUPPLY_MV 3300
#define BIKE_RFID_CS_GPIO 7
#define BIKE_RFID_SCK_GPIO 4
#define BIKE_RFID_MISO_GPIO 5
#define BIKE_RFID_MOSI_GPIO 6
#define BIKE_RFID_RST_GPIO 10

/* UID values are configured in menuconfig > Bike RFID authorization. */
#define BIKE_RFID_AUTH_POLICY_HARDCODED 1
