#include "hardware_test.h"

#include "esp_log.h"
#include "hardware_test_internal.h"

static const char *TAG = "hardware_test";

bool hardware_test_start(const hardware_test_config_t *config) {
  if (config == NULL) {
    ESP_LOGE(TAG, "Test config is NULL");
    return false;
  }

  ESP_LOGW(TAG, "Hardware test mode enabled, phase=%d", config->phase);

  switch (config->phase) {
    case 1:
      return phase1_board_test_start();
    case 2:
      return phase2_mpu6050_test_start(config);
    case 3:
      return phase3_gps_test_start(config);
    case 4:
      return phase4_mq3_test_start(config);
    case 5:
      return phase5_accident_test_start(config);
    case 6:
      return phase6_fsm_test_start();
    case 20:
      return i2c_oled_test_start(config);
    default:
      ESP_LOGE(TAG, "Unsupported test: %d (expected 1..6 or 20)",
               config->phase);
      return false;
  }
}
