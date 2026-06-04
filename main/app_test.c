#include "app_test.h"

#include "esp_log.h"
#include "event_manager.h"
#include "gps_driver.h"
#include "mpu6050_driver.h"
#include "mq3_driver.h"
#include "system_config.h"

#define MOCK_IMPACT_START_MS 2500
#define MOCK_IMPACT_END_MS 2600
#define MOCK_FALL_START_MS 4000
#define MOCK_FALL_END_MS 7000
#define MOCK_EVENT_ALCOHOL_PASS_MS 2000
#define MOCK_EVENT_SOS_PRESSED_MS 8000

static const char *TAG = "app_test";

void app_test_run_startup_checks(void) {
  if (USE_MOCK_SENSOR_DATA == 0) {
    return;
  }

  ESP_LOGI(TAG, "Running mock startup checks");
  app_test_mpu6050_once();
  app_test_gps_once();
  app_test_mq3_once();
}

bool app_test_mpu6050_once(void) {
  if (USE_MOCK_SENSOR_DATA == 0) {
    return true;
  }

  uint8_t who_am_i = 0;
  mpu6050_raw_t raw = {0};

  bool ok = mpu6050_read_who_am_i(&who_am_i) && mpu6050_read_raw(&raw);
  if (ok) {
    ESP_LOGI(TAG, "MPU6050 test WHO_AM_I=0x%02X raw=(%d,%d,%d,%d,%d,%d)",
             who_am_i, raw.ax, raw.ay, raw.az, raw.gx, raw.gy, raw.gz);
  } else {
    ESP_LOGW(TAG, "MPU6050 mock startup check failed");
  }

  return ok;
}

bool app_test_gps_once(void) {
  if (USE_MOCK_SENSOR_DATA == 0) {
    return true;
  }

  char line[GPS_RAW_LINE_BUFFER_SIZE] = {0};
  gps_data_t data = {0};

  int line_len = gps_read_raw_line(line, sizeof(line), GPS_READ_TIMEOUT_MS);
  bool ok = (line_len > 0) && gps_read_data(&data);
  if (ok) {
    ESP_LOGI(TAG, "GPS test raw=%s", line);
    ESP_LOGI(TAG, "GPS test fix=%d lat=%.6f lon=%.6f sats=%u hdop=%.1f",
             data.fix_valid, data.latitude, data.longitude, data.satellites,
             data.hdop);
  } else {
    ESP_LOGW(TAG, "GPS mock startup check failed");
  }

  return ok;
}

bool app_test_mq3_once(void) {
  if (USE_MOCK_SENSOR_DATA == 0) {
    return true;
  }

  mq3_power_on();

  uint16_t raw = 0;
  float avg_voltage = 0.0f;
  bool ok = mq3_read_raw(&raw) &&
            mq3_sample_average(&avg_voltage, MQ3_TEST_SAMPLE_COUNT);

  if (ok) {
    ESP_LOGI(TAG, "MQ-3 test raw=%u avg=%.2fV alcohol=%d", raw, avg_voltage,
             mq3_is_alcohol_detected(avg_voltage));
  } else {
    ESP_LOGW(TAG, "MQ-3 mock startup check failed");
  }

  mq3_power_off();
  return ok;
}

bool app_test_get_mock_imu_sample(float *ax, float *ay, float *az,
                                  uint32_t elapsed_ms) {
  if ((USE_MOCK_SENSOR_DATA == 0) || (ax == NULL) || (ay == NULL) ||
      (az == NULL)) {
    return false;
  }

  if ((elapsed_ms >= MOCK_IMPACT_START_MS) &&
      (elapsed_ms < MOCK_IMPACT_END_MS)) {
    *ax = 3.6f;
    *ay = 0.2f;
    *az = 0.4f;
    return true;
  }

  if ((elapsed_ms >= MOCK_FALL_START_MS) && (elapsed_ms < MOCK_FALL_END_MS)) {
    *ax = 1.0f;
    *ay = 0.0f;
    *az = 0.1f;
    return true;
  }

  *ax = 0.0f;
  *ay = 0.0f;
  *az = 1.0f;
  return true;
}

void app_test_publish_mock_events_if_needed(uint32_t elapsed_ms) {
  static bool alcohol_pass_sent;
  static bool sos_pressed_sent;

  if (USE_MOCK_SENSOR_DATA == 0) {
    return;
  }

  if (!alcohol_pass_sent && (elapsed_ms >= MOCK_EVENT_ALCOHOL_PASS_MS)) {
    event_manager_publish(SYSTEM_EVENT_ALCOHOL_PASS);
    alcohol_pass_sent = true;
  }

  if (!sos_pressed_sent && (elapsed_ms >= MOCK_EVENT_SOS_PRESSED_MS)) {
    event_manager_publish(SYSTEM_EVENT_SOS_PRESSED);
    sos_pressed_sent = true;
  }
}
