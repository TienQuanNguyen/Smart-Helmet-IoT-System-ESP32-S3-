#include "rfid_reader.h"

#include <string.h>
#include "driver/rc522_spi.h"
#include "freertos/FreeRTOS.h"
#include "rc522.h"
#include "rc522_picc.h"

#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "rfid_reader";

static bool s_initialized, s_attempted, s_present;
static esp_err_t s_init_error = ESP_ERR_INVALID_STATE;
static rc522_driver_handle_t s_driver;
static rc522_handle_t s_scanner;
static spi_bus_config_t s_bus;
static rfid_reader_uid_t s_uid;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static void card_changed(void *arg, esp_event_base_t base, int32_t id,
                         void *data) {
  (void)arg;
  (void)base;
  (void)id;
  const rc522_picc_state_changed_event_t *event = data;
  const rc522_picc_t *picc = event->picc;
  bool present = picc->state == RC522_PICC_STATE_ACTIVE ||
                 picc->state == RC522_PICC_STATE_ACTIVE_H;
  size_t length = picc->uid.length;
  present = present && (length == 4 || length == 7 || length == 10);
  portENTER_CRITICAL(&s_lock);
  memset(&s_uid, 0, sizeof(s_uid));
  s_present = present;
  if (present) {
    s_uid.length = length;
    memcpy(s_uid.bytes, picc->uid.value, length);
  }
  portEXIT_CRITICAL(&s_lock);
  if (!present && (event->old_state == RC522_PICC_STATE_ACTIVE ||
             event->old_state == RC522_PICC_STATE_ACTIVE_H)) {
    ESP_LOGI(TAG, "Card removed or communication lost");
  }
}

esp_err_t rfid_reader_init(const rfid_reader_config_t *config) {
  ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG,
                      "RFID config is NULL");
  ESP_RETURN_ON_FALSE(config->clock_speed_hz > 0 &&
                          config->clock_speed_hz <= 10000000,
                      ESP_ERR_INVALID_ARG, TAG, "Invalid SPI clock");
  ESP_RETURN_ON_FALSE(GPIO_IS_VALID_OUTPUT_GPIO(config->cs_gpio) &&
                          GPIO_IS_VALID_OUTPUT_GPIO(config->sck_gpio) &&
                          GPIO_IS_VALID_GPIO(config->miso_gpio) &&
                          GPIO_IS_VALID_OUTPUT_GPIO(config->mosi_gpio) &&
                          GPIO_IS_VALID_OUTPUT_GPIO(config->rst_gpio),
                      ESP_ERR_INVALID_ARG, TAG, "Invalid RFID pins");
  int pins[] = {config->cs_gpio, config->sck_gpio, config->miso_gpio,
                config->mosi_gpio, config->rst_gpio};
  for (unsigned i = 0; i < 5; ++i)
    for (unsigned j = i + 1; j < 5; ++j)
      ESP_RETURN_ON_FALSE(pins[i] != pins[j], ESP_ERR_INVALID_ARG, TAG,
                          "RFID pins overlap");
  if (s_initialized) return ESP_OK;
  /* Failed initialization is terminal until reboot; avoid duplicate tasks. */
  if (s_attempted) return s_init_error;
  s_attempted = true;
  s_bus = (spi_bus_config_t){.miso_io_num = config->miso_gpio,
                             .mosi_io_num = config->mosi_gpio,
                             .sclk_io_num = config->sck_gpio,
                             .quadwp_io_num = -1, .quadhd_io_num = -1};
  rc522_spi_config_t spi = {
      .host_id = SPI2_HOST, .bus_config = &s_bus,
      .dev_config = {.spics_io_num = config->cs_gpio,
                     .clock_speed_hz = (int)config->clock_speed_hz},
      .rst_io_num = config->rst_gpio};
  s_init_error = rc522_spi_create(&spi, &s_driver);
  if (s_init_error != ESP_OK) return s_init_error;
  s_init_error = rc522_driver_install(s_driver);
  if (s_init_error != ESP_OK) return s_init_error;
  rc522_config_t scanner = {.driver = s_driver, .poll_interval_ms = 100};
  s_init_error = rc522_create(&scanner, &s_scanner);
  if (s_init_error == ESP_OK)
    s_init_error = rc522_register_events(s_scanner, RC522_EVENT_PICC_STATE_CHANGED,
                                         card_changed, NULL);
  if (s_init_error == ESP_OK) s_init_error = rc522_start(s_scanner);
  if (s_init_error != ESP_OK) {
    if (s_scanner) {
      rc522_destroy(s_scanner);
      s_scanner = NULL;
    }
    rc522_driver_uninstall(s_driver);
    ESP_LOGE(TAG, "RC522 init failed: %s", esp_err_to_name(s_init_error));
    return s_init_error;
  }
  s_initialized = true;
  ESP_LOGI(TAG, "RC522 SPI ready; waiting for a card");
  return ESP_OK;
}

bool rfid_reader_is_initialized(void) {
  return s_initialized;
}

esp_err_t rfid_reader_poll_uid(rfid_reader_uid_t *uid, bool *card_present) {
  ESP_RETURN_ON_FALSE(uid != NULL, ESP_ERR_INVALID_ARG, TAG, "UID is NULL");
  ESP_RETURN_ON_FALSE(card_present != NULL, ESP_ERR_INVALID_ARG, TAG,
                      "card_present is NULL");
  memset(uid, 0, sizeof(*uid));
  *card_present = false;
  ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG,
                      "RFID reader is not initialized");

  portENTER_CRITICAL(&s_lock);
  *uid = s_uid;
  *card_present = s_present;
  portEXIT_CRITICAL(&s_lock);
  return ESP_OK;
}
