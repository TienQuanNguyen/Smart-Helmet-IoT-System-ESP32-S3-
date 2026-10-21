#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "system_state.h"

typedef struct {
    const char *source_event;
    uint32_t uptime_ms;
    system_state_t state;
} cloud_client_emergency_report_t;

bool cloud_client_init(void);
bool cloud_client_submit_emergency(
    const cloud_client_emergency_report_t *report);
bool cloud_client_is_enabled(void);

/* Latest measured state, not an offline telemetry queue. SOS is latched for
 * the current emergency, not the instantaneous electrical button level. */
typedef struct {
    system_state_t state;
    uint32_t uptime_ms;
    bool imu_valid, alcohol_valid, fall_detected, sos_latched;
    float accel_mag_g, alcohol_voltage;
    uint16_t alcohol_raw;
    bool alcohol_detected;
} cloud_client_telemetry_t;
bool cloud_client_update_telemetry(const cloud_client_telemetry_t *sample);
