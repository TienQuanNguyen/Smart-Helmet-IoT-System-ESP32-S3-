#include "bike_app.h"

#include <inttypes.h>
#include <stdio.h>

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
#include "sdkconfig.h"

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
      .clock_speed_hz = 100000U,
  };
  ESP_RETURN_ON_ERROR(rfid_reader_init(&rfid_config), TAG,
                      "RFID reader init failed");

#if !CONFIG_BIKE_RFID_UID_SCAN_ONLY
  ESP_RETURN_ON_ERROR(bike_ble_comm_init(), TAG, "Bike BLE init failed");
#endif
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
           "RFID ready; scan an allowed UID to request LOCK/UNLOCK; "
           "unlock requires fresh Helmet READY/DRIVING + alcohol PASS");
#if CONFIG_BIKE_RFID_UID_SCAN_ONLY
  ESP_LOGW(TAG, "UID SCAN ONLY: relay locked, BLE disabled");
#endif
  if (CONFIG_BIKE_RFID_UID_1[0] == '\0' && CONFIG_BIKE_RFID_UID_2[0] == '\0')
    ESP_LOGW(TAG, "RFID allowlist empty: all cards denied");

  s_initialized = true;
  return ESP_OK;
}

void bike_app_run(void) {
  if (!s_initialized) {
    ESP_LOGE(TAG, "bike_app_run called before bike_app_init");
    return;
  }

  uint32_t last_log_ms = 0;
  bool scan_armed = true;
  uint32_t last_present_ms = 0;
  bool last_ignition_allowed = false;
  while (true) {
    const uint32_t uptime_ms = (uint32_t)(esp_timer_get_time() / 1000LL);
    rfid_reader_uid_t uid;
    bool card_present = false;
    esp_err_t rfid_result = rfid_reader_poll_uid(&uid, &card_present);
    ble_helmet_status_packet_t status;
    bool fresh = bike_ble_comm_get_status(&status);
    bool ready = fresh &&
                 (status.state == BLE_HELMET_STATE_READY ||
                  status.state == BLE_HELMET_STATE_DRIVING) &&
                 !(status.flags & BLE_HELMET_FLAG_EMERGENCY);
    bike_state_update_helmet(
        fresh, ready, fresh && (status.flags & BLE_HELMET_FLAG_ALCOHOL_PASS));
    if (rfid_result != ESP_OK) {
      bike_state_set_rfid_authorized(false);
      scan_armed = false;
      last_present_ms = uptime_ms;
    } else if (card_present) {
      last_present_ms = uptime_ms;
      if (scan_armed) {
        scan_armed = false;
        /* Log the captured UID once per armed scan, not per driver event. */
        char uid_text[RFID_READER_MAX_UID_LEN * 3];
        for (size_t i = 0; i < uid.length; ++i)
          snprintf(uid_text + i * 3, sizeof(uid_text) - i * 3, "%02X%s",
                   (unsigned)uid.bytes[i], i + 1 < uid.length ? ":" : "");
        ESP_LOGI(TAG, "Card UID=%s (%u bytes)", uid_text, (unsigned)uid.length);
#if CONFIG_BIKE_RFID_UID_SCAN_ONLY
        ESP_LOGI(TAG, "UID capture only: relay request denied");
#else
        if (!bike_state_is_hardcoded_uid_allowed(uid.bytes, uid.length)) {
          ESP_LOGW(TAG, "RFID DENIED: UID is not in allowlist");
        } else if (!bike_state_request_card(uid.bytes, uid.length)) {
          ESP_LOGW(TAG, "RFID DENIED: Helmet not ready, stale or alcohol failed");
        } else {
          ESP_LOGI(TAG, "RFID request accepted: %s",
                   bike_state_is_ignition_allowed() ? "UNLOCK" : "LOCK");
        }
#endif
      }
    } else if (uptime_ms - last_present_ms >= 400) {
      scan_armed = true;
    }
#if CONFIG_BIKE_RFID_UID_SCAN_ONLY
    bike_state_set_rfid_authorized(false);
#endif
    bool allowed = bike_state_is_ignition_allowed();
    if (relay_driver_set_ignition_allowed(allowed) != ESP_OK) {
      bike_state_set_rfid_authorized(false);
      ESP_LOGE(TAG, "Relay update failed; authorization revoked");
      relay_driver_set_ignition_allowed(false);
      return;
    }
    if (allowed != last_ignition_allowed) {
      ESP_LOGI(TAG, "Relay ignition_allowed=%d", allowed);
      last_ignition_allowed = allowed;
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
