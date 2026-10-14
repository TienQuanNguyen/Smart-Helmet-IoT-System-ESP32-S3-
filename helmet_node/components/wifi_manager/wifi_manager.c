#include "wifi_manager.h"

#include <stdatomic.h>
#include <string.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "event_manager.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#ifndef CONFIG_SMART_HELMET_WIFI_SSID
#define CONFIG_SMART_HELMET_WIFI_SSID ""
#endif

#ifndef CONFIG_SMART_HELMET_WIFI_PASSWORD
#define CONFIG_SMART_HELMET_WIFI_PASSWORD ""
#endif

#ifndef CONFIG_SMART_HELMET_WIFI_MAX_RETRY
#define CONFIG_SMART_HELMET_WIFI_MAX_RETRY 5
#endif

static const char *TAG = "wifi_manager";

static bool s_initialized;
static bool s_enabled;
static atomic_bool s_started;
static atomic_bool s_connected;
static esp_timer_handle_t s_retry_timer;
static atomic_uchar s_retry_count;
static uint8_t s_max_retry_count;
static esp_netif_t *s_netif;

static void schedule_reconnect(void) {
  if (!s_started) return;
  unsigned exponent = s_retry_count < 6 ? s_retry_count : 6;
  uint32_t delay_ms = 1000U << exponent;
  if (delay_ms > 60000 || s_retry_count >= s_max_retry_count) delay_ms = 60000;
  if (s_retry_count < 6) ++s_retry_count;
  esp_timer_stop(s_retry_timer);
  if (esp_timer_start_once(s_retry_timer, (uint64_t)delay_ms * 1000) != ESP_OK)
    ESP_LOGE(TAG, "Cannot schedule reconnect");
}

static void reconnect_timer(void *arg) {
  (void)arg;
  if (s_started && !s_connected && esp_wifi_connect() != ESP_OK)
    schedule_reconnect();
}

static bool wifi_manager_check_err(esp_err_t err, const char *action) {
  if ((err == ESP_OK) || (err == ESP_ERR_INVALID_STATE)) {
    return true;
  }

  ESP_LOGE(TAG, "%s failed: %s", action, esp_err_to_name(err));
  return false;
}

static bool wifi_manager_has_text(const char *value) {
  return (value != NULL) && (value[0] != '\0');
}

static void wifi_manager_publish_loss_if_needed(void) {
  if (s_connected) {
    s_connected = false;
    if (!event_manager_publish(SYSTEM_EVENT_WIFI_LOST))
      ESP_LOGW(TAG, "Wi-Fi loss event queue full");
  }
}

static void wifi_manager_event_handler(void *arg, esp_event_base_t event_base,
                                       int32_t event_id, void *event_data) {
  (void)arg;
  (void)event_data;

  if ((event_base == WIFI_EVENT) && (event_id == WIFI_EVENT_STA_START)) {
    if (esp_wifi_connect() != ESP_OK) schedule_reconnect();
    return;
  }

  if ((event_base == WIFI_EVENT) && (event_id == WIFI_EVENT_STA_DISCONNECTED)) {
    wifi_manager_publish_loss_if_needed();

    schedule_reconnect();
    return;
  }

  if ((event_base == IP_EVENT) && (event_id == IP_EVENT_STA_GOT_IP)) {
    s_retry_count = 0;
    if (!s_connected) {
      s_connected = true;
      if (!event_manager_publish(SYSTEM_EVENT_WIFI_CONNECTED))
        ESP_LOGW(TAG, "Wi-Fi connected event queue full");
    }
    ESP_LOGI(TAG, "Wi-Fi connected");
  }
}

static bool wifi_manager_init_nvs(void) {
  esp_err_t err = nvs_flash_init();
  if ((err == ESP_OK) || (err == ESP_ERR_NVS_NO_FREE_PAGES) ||
      (err == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "NVS needs maintenance before Wi-Fi can start: %s",
               esp_err_to_name(err));
      return false;
    }
    return true;
  }

  ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
  return false;
}

bool wifi_manager_init(const wifi_manager_config_t *config) {
  if (s_initialized) {
    return true;
  }

  const bool enabled =
#if CONFIG_SMART_HELMET_WIFI_ENABLE
      (config == NULL) ? true : config->enabled;
#else
      false;
#endif
  const char *ssid =
      (config != NULL) ? config->ssid : CONFIG_SMART_HELMET_WIFI_SSID;
  const char *password =
      (config != NULL) ? config->password : CONFIG_SMART_HELMET_WIFI_PASSWORD;
  s_max_retry_count = (config != NULL)
                          ? config->max_retry_count
                          : (uint8_t)CONFIG_SMART_HELMET_WIFI_MAX_RETRY;

  if (!enabled || !wifi_manager_has_text(ssid)) {
    s_enabled = false;
    s_initialized = true;
    ESP_LOGW(TAG,
             "Wi-Fi STA disabled or phone hotspot SSID not configured; cloud "
             "path is unavailable");
    return true;
  }

  if (!wifi_manager_init_nvs() ||
      !wifi_manager_check_err(esp_netif_init(), "esp_netif_init") ||
      !wifi_manager_check_err(esp_event_loop_create_default(),
                              "esp_event_loop_create_default")) {
    return false;
  }

  s_netif = esp_netif_create_default_wifi_sta();
  if (s_netif == NULL) {
    ESP_LOGE(TAG, "Failed to create default Wi-Fi STA netif");
    return false;
  }

  wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
  if (!wifi_manager_check_err(esp_wifi_init(&wifi_init_config),
                              "esp_wifi_init") ||
      !wifi_manager_check_err(esp_event_handler_instance_register(
                                  WIFI_EVENT, ESP_EVENT_ANY_ID,
                                  &wifi_manager_event_handler, NULL, NULL),
                              "register Wi-Fi handler") ||
      !wifi_manager_check_err(esp_event_handler_instance_register(
                                  IP_EVENT, IP_EVENT_STA_GOT_IP,
                                  &wifi_manager_event_handler, NULL, NULL),
                              "register IP handler")) {
    return false;
  }

  wifi_config_t wifi_config = {0};
  strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
  if (wifi_manager_has_text(password)) {
    strncpy((char *)wifi_config.sta.password, password,
            sizeof(wifi_config.sta.password) - 1);
  }

  if (!wifi_manager_check_err(esp_wifi_set_mode(WIFI_MODE_STA),
                              "esp_wifi_set_mode") ||
      !wifi_manager_check_err(esp_wifi_set_config(WIFI_IF_STA, &wifi_config),
                              "esp_wifi_set_config")) {
    return false;
  }

  s_enabled = true;
  esp_timer_create_args_t timer = {.callback = reconnect_timer,
                                   .name = "wifi_retry"};
  if (esp_timer_create(&timer, &s_retry_timer) != ESP_OK) return false;
  s_initialized = true;
  ESP_LOGI(TAG, "Wi-Fi STA initialized for phone hotspot gateway");
  return true;
}

bool wifi_manager_start(void) {
  if (!s_initialized || !s_enabled) {
    return true;
  }

  if (s_started) {
    return true;
  }

  s_started = true;
  if (!wifi_manager_check_err(esp_wifi_start(), "esp_wifi_start")) {
    s_started = false;
    return false;
  }

  s_started = true;
  return true;
}

bool wifi_manager_stop(void) {
  if (!s_initialized || !s_enabled || !s_started) {
    return true;
  }

  s_started = false;
  esp_timer_stop(s_retry_timer);
  wifi_manager_publish_loss_if_needed();
  if (!wifi_manager_check_err(esp_wifi_stop(), "esp_wifi_stop")) {
    return false;
  }

  s_started = false;
  s_retry_count = 0;
  return true;
}

bool wifi_manager_is_enabled(void) { return s_enabled; }

bool wifi_manager_is_connected(void) { return s_connected; }
