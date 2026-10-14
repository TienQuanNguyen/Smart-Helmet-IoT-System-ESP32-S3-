#include "rfid_reader.h"

#include <inttypes.h>

#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "rfid_reader";

static rfid_reader_config_t s_config;
static bool s_initialized;

esp_err_t rfid_reader_init(const rfid_reader_config_t *config) {
  ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG,
                      "RFID config is NULL");
  ESP_RETURN_ON_FALSE(config->clock_speed_hz > 0U, ESP_ERR_INVALID_ARG, TAG,
                      "RFID SPI clock must be non-zero");

  s_config = *config;
  s_initialized = true;

  ESP_LOGI(TAG,
           "RFID reader skeleton initialized CS=%d SCK=%d MISO=%d MOSI=%d "
           "RST=%d clock=%" PRIu32,
           s_config.cs_gpio, s_config.sck_gpio, s_config.miso_gpio,
           s_config.mosi_gpio, s_config.rst_gpio, s_config.clock_speed_hz);
  ESP_LOGW(TAG, "RC522 SPI runtime is not implemented in this phase");
  return ESP_OK;
}

bool rfid_reader_is_initialized(void) {
  return s_initialized;
}

esp_err_t rfid_reader_poll_uid(rfid_reader_uid_t *uid, bool *card_present) {
  ESP_RETURN_ON_FALSE(uid != NULL, ESP_ERR_INVALID_ARG, TAG, "UID is NULL");
  ESP_RETURN_ON_FALSE(card_present != NULL, ESP_ERR_INVALID_ARG, TAG,
                      "card_present is NULL");
  ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG,
                      "RFID reader is not initialized");

  uid->length = 0U;
  *card_present = false;
  return ESP_ERR_NOT_SUPPORTED;
}
