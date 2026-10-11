#pragma once

#include <stdint.h>

#define BLE_PROTOCOL_VERSION 1U

/*
 * Final Phase 7 topology:
 * Helmet ESP32-S3: BLE peripheral / GATT server.
 * Bike ESP32-C3: BLE central / GATT client.
 */
#define BLE_HELMET_SERVICE_UUID "9f0d0001-6c83-4c0d-9b2b-48e150000001"
#define BLE_HELMET_STATUS_CHAR_UUID "9f0d0002-6c83-4c0d-9b2b-48e150000001"
#define BLE_BIKE_COMMAND_CHAR_UUID "9f0d0003-6c83-4c0d-9b2b-48e150000001"

typedef enum {
    BLE_HELMET_STATE_BOOT = 0,
    BLE_HELMET_STATE_ALCOHOL_CHECK = 1,
    BLE_HELMET_STATE_READY = 2,
    BLE_HELMET_STATE_DRIVING = 3,
    BLE_HELMET_STATE_ACCIDENT = 4,
    BLE_HELMET_STATE_EMERGENCY = 5,
    BLE_HELMET_STATE_LOW_POWER = 6,
} ble_helmet_state_t;

typedef enum {
    BLE_BIKE_COMMAND_NONE = 0,
    BLE_BIKE_COMMAND_RELAY_LOCK = 1,
    BLE_BIKE_COMMAND_RELAY_UNLOCK_ALLOWED = 2,
    BLE_BIKE_COMMAND_STATUS_ACK = 3,
} ble_bike_command_t;

typedef enum {
    BLE_HELMET_FLAG_ALCOHOL_PASS = 1U << 0,
    BLE_HELMET_FLAG_EMERGENCY = 1U << 1,
    BLE_HELMET_FLAG_GPS_VALID = 1U << 2,
} ble_helmet_status_flags_t;

typedef struct {
    uint8_t version;
    uint8_t state;
    uint8_t flags;
    uint8_t sequence;
    uint32_t uptime_ms;
} ble_helmet_status_packet_t;

typedef struct {
    uint8_t version;
    uint8_t command;
    uint8_t sequence;
    uint8_t reserved;
} ble_bike_command_packet_t;
