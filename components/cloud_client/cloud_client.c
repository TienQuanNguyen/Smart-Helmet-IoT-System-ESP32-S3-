#include "cloud_client.h"

#include <stdio.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "gps_driver.h"
#include "sdkconfig.h"
#include "wifi_manager.h"

#ifndef CONFIG_SMART_HELMET_CLOUD_URL
#define CONFIG_SMART_HELMET_CLOUD_URL ""
#endif

#ifndef CONFIG_SMART_HELMET_CLOUD_AUTH_TOKEN
#define CONFIG_SMART_HELMET_CLOUD_AUTH_TOKEN ""
#endif

#ifndef CONFIG_SMART_HELMET_CLOUD_QUEUE_LENGTH
#define CONFIG_SMART_HELMET_CLOUD_QUEUE_LENGTH 4
#endif

#ifndef CONFIG_SMART_HELMET_CLOUD_TIMEOUT_MS
#define CONFIG_SMART_HELMET_CLOUD_TIMEOUT_MS 5000
#endif

#define CLOUD_CLIENT_TASK_STACK_SIZE 6144
#define CLOUD_CLIENT_TASK_PRIORITY 4
#define CLOUD_CLIENT_PAYLOAD_SIZE 384
#define CLOUD_CLIENT_SOURCE_SIZE 32

typedef struct {
    char source_event[CLOUD_CLIENT_SOURCE_SIZE];
    uint32_t uptime_ms;
    system_state_t state;
} cloud_client_report_item_t;

static const char *TAG = "cloud_client";

static QueueHandle_t s_report_queue;
static bool s_initialized;
static bool s_enabled;

static bool cloud_client_has_text(const char *value) {
  return (value != NULL) && (value[0] != '\0');
}

#if CONFIG_SMART_HELMET_CLOUD_ENABLE
static bool cloud_client_build_payload(const cloud_client_report_item_t *report,
                                       char *payload, size_t payload_size) {
  gps_data_t gps = {0};
  bool gps_ok = gps_read_data(&gps) && gps.fix_valid;

  int written = snprintf(
      payload, payload_size,
      "{\"device\":\"%s\",\"event\":\"%s\",\"uptime_ms\":%lu,"
      "\"state\":\"%s\",\"gps\":{\"valid\":%s,\"lat\":%.6f,\"lon\":%.6f,"
      "\"satellites\":%u,\"hdop\":%.1f},"
      "\"network\":{\"path\":\"helmet_wifi_phone_hotspot_cloud\","
      "\"transport\":\"http\"}}",
      "helmet-node", report->source_event, (unsigned long)report->uptime_ms,
      system_state_to_string(report->state), gps_ok ? "true" : "false",
      gps_ok ? gps.latitude : 0.0, gps_ok ? gps.longitude : 0.0,
      gps_ok ? gps.satellites : 0, gps_ok ? gps.hdop : 0.0f);

  return (written > 0) && ((size_t)written < payload_size);
}

static bool cloud_client_post_payload(const char *payload) {
  esp_http_client_config_t config = {
      .url = CONFIG_SMART_HELMET_CLOUD_URL,
      .timeout_ms = CONFIG_SMART_HELMET_CLOUD_TIMEOUT_MS,
      .crt_bundle_attach = esp_crt_bundle_attach,
  };

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == NULL) {
    ESP_LOGE(TAG, "Failed to initialize HTTP client");
    return false;
  }

  esp_http_client_set_method(client, HTTP_METHOD_POST);
  esp_http_client_set_header(client, "Content-Type", "application/json");
  if (cloud_client_has_text(CONFIG_SMART_HELMET_CLOUD_AUTH_TOKEN)) {
    char auth_header[160] = {0};
    snprintf(auth_header, sizeof(auth_header), "Bearer %s",
             CONFIG_SMART_HELMET_CLOUD_AUTH_TOKEN);
    esp_http_client_set_header(client, "Authorization", auth_header);
  }
  esp_http_client_set_post_field(client, payload, (int)strlen(payload));

  esp_err_t err = esp_http_client_perform(client);
  int status_code = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Emergency report HTTP request failed: %s",
             esp_err_to_name(err));
    return false;
  }

  if ((status_code < 200) || (status_code >= 300)) {
    ESP_LOGE(TAG, "Emergency report rejected, status=%d", status_code);
    return false;
  }

  ESP_LOGI(TAG, "Emergency report delivered, status=%d", status_code);
  return true;
}

static void cloud_client_task(void *arg) {
  (void)arg;

  while (true) {
    cloud_client_report_item_t report = {0};
    if (xQueueReceive(s_report_queue, &report, portMAX_DELAY) != pdTRUE) {
      continue;
    }

    if (!wifi_manager_is_connected()) {
      ESP_LOGW(TAG, "Emergency report queued but Wi-Fi is not connected");
      continue;
    }

    char payload[CLOUD_CLIENT_PAYLOAD_SIZE] = {0};
    if (!cloud_client_build_payload(&report, payload, sizeof(payload))) {
      ESP_LOGE(TAG, "Emergency payload exceeded buffer");
      continue;
    }

    cloud_client_post_payload(payload);
  }
}
#endif

bool cloud_client_init(void) {
  if (s_initialized) {
    return true;
  }

#if CONFIG_SMART_HELMET_CLOUD_ENABLE
  if (!cloud_client_has_text(CONFIG_SMART_HELMET_CLOUD_URL)) {
    s_enabled = false;
    s_initialized = true;
    ESP_LOGW(TAG, "Cloud client disabled because endpoint URL is empty");
    return true;
  }

  s_report_queue =
      xQueueCreate(CONFIG_SMART_HELMET_CLOUD_QUEUE_LENGTH,
                   sizeof(cloud_client_report_item_t));
  if (s_report_queue == NULL) {
    ESP_LOGE(TAG, "Failed to create emergency report queue");
    return false;
  }

  BaseType_t task_status =
      xTaskCreate(cloud_client_task, "cloud_client",
                  CLOUD_CLIENT_TASK_STACK_SIZE, NULL,
                  CLOUD_CLIENT_TASK_PRIORITY, NULL);
  if (task_status != pdPASS) {
    ESP_LOGE(TAG, "Failed to create cloud client task");
    return false;
  }

  s_enabled = true;
  s_initialized = true;
  ESP_LOGI(TAG, "Cloud client initialized");
  return true;
#else
  s_enabled = false;
  s_initialized = true;
  ESP_LOGW(TAG, "Cloud client disabled by project config");
  return true;
#endif
}

bool cloud_client_submit_emergency(
    const cloud_client_emergency_report_t *report) {
  if ((report == NULL) || !s_initialized || !s_enabled) {
    return true;
  }

  cloud_client_report_item_t item = {
      .uptime_ms = report->uptime_ms,
      .state = report->state,
  };

  const char *source =
      cloud_client_has_text(report->source_event) ? report->source_event
                                                  : "UNKNOWN";
  strncpy(item.source_event, source, sizeof(item.source_event) - 1);

  if (xQueueSend(s_report_queue, &item, 0) != pdTRUE) {
    ESP_LOGE(TAG, "Emergency report queue is full");
    return false;
  }

  return true;
}

bool cloud_client_is_enabled(void) {
  return s_enabled;
}
