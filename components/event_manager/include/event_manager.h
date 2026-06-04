#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    SYSTEM_EVENT_NONE = 0,
    SYSTEM_EVENT_ALCOHOL_PASS,
    SYSTEM_EVENT_ALCOHOL_FAIL,
    SYSTEM_EVENT_IMU_IMPACT,
    SYSTEM_EVENT_FALL_CONFIRMED,
    SYSTEM_EVENT_GPS_VALID,
    SYSTEM_EVENT_WIFI_CONNECTED,
    SYSTEM_EVENT_WIFI_LOST,
    SYSTEM_EVENT_BLE_CONNECTED,
    SYSTEM_EVENT_SOS_PRESSED,
    SYSTEM_EVENT_BATTERY_LOW
} system_event_t;

bool event_manager_init(void);
bool event_manager_publish(system_event_t event);
bool event_manager_wait(system_event_t *event, uint32_t timeout_ms);
