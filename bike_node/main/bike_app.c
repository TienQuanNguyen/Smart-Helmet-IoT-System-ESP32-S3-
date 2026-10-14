#include "bike_app.h"

#include <inttypes.h>

#include "bike_ble_comm.h"
#include "bike_config.h"
#include "bike_state.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "relay_driver.h"
#include "rfid_reader.h"

static const char *TAG = "bike_app";

static bool s_initialized;

static esp_err_t bike_app_set_relay_safe_state(void) {
  const relay_driver_config_t relay_config = {
      .gpio = BIKE_RELAY_GPIO,
      .active_level = BIKE_RELAY_ACTIVE_LEVEL,
      .inactive_level = BIKE_RELAY_INACTIVE_LEVEL,
  };

  ESP_RETURN_ON_ERROR(relay_driver_init(&relay_config), TAG,
                      "Relay safe-state setup failed");
  return ESP_OK;
}

static esp_err_t bike_app_init_components(void) {
  ESP_RETURN_ON_ERROR(bike_state_init(), TAG, "Bike state init failed");

  const rfid_reader_config_t rfid_config = {
      .cs_gpio = BIKE_RFID_CS_GPIO,
      .sck_gpio = BIKE_RFID_SCK_GPIO,
      .miso_gpio = BIKE_RFID_MISO_GPIO,
      .mosi_gpio = BIKE_RFID_MOSI_GPIO,
      .rst_gpio = BIKE_RFID_RST_GPIO,
      .clock_speed_hz = 1000000U,
  };
  ESP_RETURN_ON_ERROR(rfid_reader_init(&rfid_config), TAG,
                      "RFID reader init failed");

  ESP_RETURN_ON_ERROR(bike_ble_comm_init(), TAG, "Bike BLE init failed");
  return ESP_OK;
}

static void bike_app_log_pin_map(void) {
  ESP_LOGI(TAG, "Board: %s", BIKE_NODE_BOARD);
  ESP_LOGI(TAG, "Relay GPIO=%d active_level=%d safe_level=%d", BIKE_RELAY_GPIO,
           BIKE_RELAY_ACTIVE_LEVEL, BIKE_RELAY_INACTIVE_LEVEL);
  ESP_LOGI(TAG, "OLED required=%d SDA=%d SCL=%d address=0x%02X",
           BIKE_OLED_REQUIRED, BIKE_OLED_SDA_GPIO, BIKE_OLED_SCL_GPIO,
           BIKE_OLED_ADDR);
  ESP_LOGI(TAG, "RC522 CS=%d SCK=%d MISO=%d MOSI=%d RST=%d supply=%dmV",
           BIKE_RFID_CS_GPIO, BIKE_RFID_SCK_GPIO, BIKE_RFID_MISO_GPIO,
           BIKE_RFID_MOSI_GPIO, BIKE_RFID_RST_GPIO, BIKE_RFID_SUPPLY_MV);
}

esp_err_t bike_app_init(void) {
  if (s_initialized) {
    return ESP_OK;
  }

  ESP_RETURN_ON_ERROR(bike_app_set_relay_safe_state(), TAG,
                      "Relay safe-state setup failed");
  ESP_RETURN_ON_ERROR(bike_app_init_components(), TAG,
                      "Bike component setup failed");

  bike_app_log_pin_map();
  ESP_LOGI(TAG,
           "BLE runtime ready; RFID UID and OLED validation remain BLOCKED");

  s_initialized = true;
  return ESP_OK;
}

void bike_app_run(void) {
  if (!s_initialized) {
    ESP_LOGE(TAG, "bike_app_run called before bike_app_init");
    return;
  }

  uint32_t last_log_ms = 0;
  while (true) {
    const uint32_t uptime_ms = (uint32_t)(esp_timer_get_time() / 1000LL);
    ble_helmet_status_packet_t status;
    bool fresh = bike_ble_comm_get_status(&status);
    bool ready = fresh &&
                 (status.state == BLE_HELMET_STATE_READY ||
                  status.state == BLE_HELMET_STATE_DRIVING) &&
                 !(status.flags & BLE_HELMET_FLAG_EMERGENCY);
    bike_state_update_helmet(
        fresh, ready, fresh && (status.flags & BLE_HELMET_FLAG_ALCOHOL_PASS));
    if (relay_driver_set_ignition_allowed(bike_state_is_ignition_allowed()) !=
        ESP_OK) {
      bike_state_set_rfid_authorized(false);
      ESP_LOGE(TAG, "Relay update failed; authorization revoked");
    }
    if (uptime_ms - last_log_ms >= BIKE_HEARTBEAT_PERIOD_MS) {
      ESP_LOGI(TAG,
               "BIKE-001 heartbeat uptime_ms=%" PRIu32 " free_heap=%" PRIu32
               " state=%s ignition_allowed=%d",
               uptime_ms, esp_get_free_heap_size(),
               bike_state_status_to_string(bike_state_get_status()),
               bike_state_is_ignition_allowed() ? 1 : 0);
      last_log_ms = uptime_ms;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
