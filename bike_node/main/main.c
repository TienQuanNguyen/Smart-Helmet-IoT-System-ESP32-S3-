#include "bike_app.h"

#include "bike_config.h"
#include "esp_err.h"
#include "esp_log.h"
#include "bike_hardware_test.h"

static const char *TAG = BIKE_NODE_NAME;

void app_main(void) {
  ESP_LOGI(TAG, "Booting %s", BIKE_NODE_NAME);

  if (BIKE_HARDWARE_TEST_MODE != 0) {
    const bike_hardware_test_config_t test_config = {
        .phase = BIKE_HARDWARE_TEST_PHASE,
        .relay_gpio = BIKE_RELAY_GPIO,
        .relay_active_level = BIKE_RELAY_ACTIVE_LEVEL,
        .oled_sda_gpio = BIKE_OLED_SDA_GPIO,
        .oled_scl_gpio = BIKE_OLED_SCL_GPIO,
        .oled_addr = BIKE_OLED_ADDR,
        .rfid_cs_gpio = BIKE_RFID_CS_GPIO,
        .rfid_sck_gpio = BIKE_RFID_SCK_GPIO,
        .rfid_miso_gpio = BIKE_RFID_MISO_GPIO,
        .rfid_mosi_gpio = BIKE_RFID_MOSI_GPIO,
        .rfid_rst_gpio = BIKE_RFID_RST_GPIO,
    };

    const esp_err_t test_result = bike_hardware_test_start(&test_config);
    if (test_result != ESP_OK) {
      ESP_LOGE(TAG, "Bike hardware test failed to start: %s",
               esp_err_to_name(test_result));
    }
    return;
  }

  const esp_err_t init_result = bike_app_init();
  if (init_result != ESP_OK) {
    ESP_LOGE(TAG, "Bike app init failed: %s", esp_err_to_name(init_result));
    return;
  }

  bike_app_run();
}
