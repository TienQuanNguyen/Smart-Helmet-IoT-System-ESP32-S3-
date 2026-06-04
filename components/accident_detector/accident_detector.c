#include "accident_detector.h"

#include <math.h>
#include <string.h>

#include "esp_log.h"
#include "system_config.h"

#define ACCIDENT_DETECTOR_PI 3.14159265358979323846f

static const char *TAG = "accident_detector";

static accident_status_t s_status = ACCIDENT_STATUS_NORMAL;
static uint32_t s_fall_candidate_start_ms;
static bool s_fall_candidate_active;

static float accident_calc_magnitude(float ax_g, float ay_g, float az_g) {
  return sqrtf((ax_g * ax_g) + (ay_g * ay_g) + (az_g * az_g));
}

static float accident_calc_tilt_angle(float ax_g, float ay_g, float az_g) {
  float horizontal_g = sqrtf((ax_g * ax_g) + (ay_g * ay_g));
  return atan2f(horizontal_g, fabsf(az_g)) * 180.0f / ACCIDENT_DETECTOR_PI;
}

bool accident_detector_init(void) {
  accident_detector_reset();
  ESP_LOGI(TAG, "Accident detector initialized");
  return true;
}

bool accident_detector_update(float ax_g, float ay_g, float az_g,
                              uint32_t timestamp_ms, accident_result_t *out) {
  if (out == NULL) {
    ESP_LOGE(TAG, "Accident result output is NULL");
    return false;
  }

  memset(out, 0, sizeof(*out));
  out->accel_magnitude_g = accident_calc_magnitude(ax_g, ay_g, az_g);
  out->tilt_angle_deg = accident_calc_tilt_angle(ax_g, ay_g, az_g);
  out->impact_detected =
      out->accel_magnitude_g > ACCIDENT_ACCEL_THRESHOLD_G;

  if (out->impact_detected) {
    s_status = ACCIDENT_STATUS_IMPACT_DETECTED;
  }

  if (out->tilt_angle_deg > FALL_TILT_THRESHOLD_DEG) {
    if (!s_fall_candidate_active) {
      s_fall_candidate_active = true;
      s_fall_candidate_start_ms = timestamp_ms;
    }

    uint32_t fall_duration_ms = timestamp_ms - s_fall_candidate_start_ms;
    if (fall_duration_ms >= FALL_CONFIRM_TIME_MS) {
      out->fall_confirmed = true;
      s_status = ACCIDENT_STATUS_FALL_CONFIRMED;
    } else if (!out->impact_detected) {
      s_status = ACCIDENT_STATUS_FALL_CANDIDATE;
    }
  } else {
    s_fall_candidate_active = false;
    s_fall_candidate_start_ms = 0;
    if (!out->impact_detected) {
      s_status = ACCIDENT_STATUS_NORMAL;
    }
  }

  return true;
}

accident_status_t accident_detector_get_status(void) {
  return s_status;
}

void accident_detector_reset(void) {
  s_status = ACCIDENT_STATUS_NORMAL;
  s_fall_candidate_start_ms = 0;
  s_fall_candidate_active = false;
}
