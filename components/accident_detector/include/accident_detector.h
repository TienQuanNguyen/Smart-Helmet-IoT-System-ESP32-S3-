#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ACCIDENT_STATUS_NORMAL = 0,
    ACCIDENT_STATUS_IMPACT_DETECTED,
    ACCIDENT_STATUS_FALL_CANDIDATE,
    ACCIDENT_STATUS_FALL_CONFIRMED
} accident_status_t;

typedef struct {
    float accel_magnitude_g;
    float tilt_angle_deg;
    bool impact_detected;
    bool fall_confirmed;
} accident_result_t;

bool accident_detector_init(void);
bool accident_detector_update(float ax_g, float ay_g, float az_g,
                              uint32_t timestamp_ms, accident_result_t *out);
accident_status_t accident_detector_get_status(void);
void accident_detector_reset(void);
