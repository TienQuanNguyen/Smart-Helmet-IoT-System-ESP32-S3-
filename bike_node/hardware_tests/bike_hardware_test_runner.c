#include "bike_hardware_test.h"

#include "esp_check.h"
#include "esp_log.h"
#include "relay_driver.h"

static const char *TAG = "bike_hw_test";

esp_err_t bike_hardware_test_start(const bike_hardware_test_config_t *config) {
  ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG,
                      "Hardware test config is NULL");

  const relay_driver_config_t relay_config = {
      .gpio = config->relay_gpio,
      .active_level = config->relay_active_level,
      .inactive_level = config->relay_active_level ? 0 : 1,
  };
  ESP_RETURN_ON_ERROR(relay_driver_init(&relay_config), TAG,
                      "Relay safe-state setup failed");

  ESP_LOGI(TAG, "Bike hardware test phase=%d", config->phase);
  ESP_LOGI(TAG, "Relay GPIO=%d active=%d", config->relay_gpio,
           config->relay_active_level);
  ESP_LOGI(TAG, "OLED SDA=%d SCL=%d addr=0x%02X", config->oled_sda_gpio,
           config->oled_scl_gpio, config->oled_addr);
  ESP_LOGI(TAG, "RC522 CS=%d SCK=%d MISO=%d MOSI=%d RST=%d",
           config->rfid_cs_gpio, config->rfid_sck_gpio,
           config->rfid_miso_gpio, config->rfid_mosi_gpio,
           config->rfid_rst_gpio);
  ESP_LOGW(TAG,
           "Only BIKE-001 skeleton is available; RC522/OLED/BLE hardware "
           "tests are BLOCKED until their modules are implemented");
  return ESP_OK;
}
