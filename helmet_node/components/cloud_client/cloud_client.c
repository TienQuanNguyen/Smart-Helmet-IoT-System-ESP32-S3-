#include "cloud_client.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "gps_driver.h"
#include "nvs.h"
#include "nvs_flash.h"
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

#define OUTBOX_VERSION 1
typedef struct {
  uint32_t id, uptime_ms;
  system_state_t state;
  gps_snapshot_t gps;
  char source[32];
} report_t;
typedef struct {
  uint32_t version, count;
  report_t items[CONFIG_SMART_HELMET_CLOUD_QUEUE_LENGTH];
} outbox_t;

static const char *TAG = "cloud_client";
static outbox_t s_outbox;
static SemaphoreHandle_t s_lock;
static nvs_handle_t s_nvs;
static bool s_initialized, s_enabled;

/* A committed blob is the acceptance boundary. Never erase NVS on failure. */
static bool save_outbox(const outbox_t *box) {
  esp_err_t err = nvs_set_blob(s_nvs, "pending", box, sizeof(*box));
  if (err == ESP_OK) err = nvs_commit(s_nvs);
  if (err != ESP_OK)
    ESP_LOGE(TAG, "Outbox persistence failed: %s", esp_err_to_name(err));
  return err == ESP_OK;
}

static bool build_payload(const report_t *report, char *payload, size_t size) {
  cJSON *root = cJSON_CreateObject();
  if (!root) return false;
  const gps_data_t *gps = &report->gps.data;
  cJSON *location = cJSON_AddObjectToObject(root, "gps");
  bool ok =
      location &&
      cJSON_AddStringToObject(root, "device", CONFIG_SMART_HELMET_DEVICE_ID) &&
      cJSON_AddNumberToObject(root, "report_id", report->id) &&
      cJSON_AddStringToObject(root, "event", report->source) &&
      cJSON_AddStringToObject(root, "state",
                              system_state_to_string(report->state)) &&
      cJSON_AddNumberToObject(root, "uptime_ms", report->uptime_ms) &&
      cJSON_AddBoolToObject(location, "valid", gps->fix_valid) &&
      cJSON_AddNumberToObject(location, "lat",
                              gps->fix_valid ? gps->latitude : 0) &&
      cJSON_AddNumberToObject(location, "lon",
                              gps->fix_valid ? gps->longitude : 0) &&
      cJSON_AddNumberToObject(location, "satellites", gps->satellites) &&
      cJSON_AddNumberToObject(location, "hdop", gps->hdop) &&
      cJSON_AddNumberToObject(location, "age_ms", report->gps.age_ms);
  if (ok) ok = cJSON_PrintPreallocated(root, payload, (int)size, false);
  cJSON_Delete(root);
  return ok;
}

static bool post_payload(const char *payload) {
  esp_http_client_config_t config = {
      .url = CONFIG_SMART_HELMET_CLOUD_URL,
      .timeout_ms = CONFIG_SMART_HELMET_CLOUD_TIMEOUT_MS,
      .crt_bundle_attach = esp_crt_bundle_attach,
      .disable_auto_redirect = true};
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) return false;
  esp_err_t err = esp_http_client_set_method(client, HTTP_METHOD_POST);
  if (err == ESP_OK)
    err =
        esp_http_client_set_header(client, "Content-Type", "application/json");
  char auth[256];
  if (err == ESP_OK && CONFIG_SMART_HELMET_CLOUD_AUTH_TOKEN[0]) {
    int len = snprintf(auth, sizeof(auth), "Bearer %s",
                       CONFIG_SMART_HELMET_CLOUD_AUTH_TOKEN);
    if (len < 0 || (size_t)len >= sizeof(auth))
      err = ESP_ERR_INVALID_SIZE;
    else
      err = esp_http_client_set_header(client, "Authorization", auth);
  }
  if (err == ESP_OK)
    err = esp_http_client_set_post_field(client, payload, strlen(payload));
  if (err == ESP_OK) err = esp_http_client_perform(client);
  int status = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);
  if (err == ESP_OK && status >= 200 && status < 300) {
    ESP_LOGI(TAG, "Emergency report delivered, status=%d", status);
    return true;
  }
  ESP_LOGW(TAG, "HTTP delivery failed: error=%s status=%d",
           esp_err_to_name(err), status);
  return false;
}

static void cloud_task(void *arg) {
  (void)arg;
  uint32_t backoff_ms = 1000;
  for (;;) {
    report_t report = {0};
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool pending = s_outbox.count > 0;
    if (pending) report = s_outbox.items[0];
    xSemaphoreGive(s_lock);
    if (!pending) {
      vTaskDelay(pdMS_TO_TICKS(250));
      continue;
    }
    char payload[640];
    bool sent = CONFIG_SMART_HELMET_CLOUD_URL[0] &&
                wifi_manager_is_connected() &&
                build_payload(&report, payload, sizeof(payload)) &&
                post_payload(payload);
    if (sent) {
      xSemaphoreTake(s_lock, portMAX_DELAY);
      /* Full-queue policy can replace the in-flight item while HTTP runs. */
      if (s_outbox.count && s_outbox.items[0].id == report.id) {
        outbox_t next = s_outbox;
        --next.count;
        memmove(next.items, next.items + 1, next.count * sizeof(report_t));
        memset(&next.items[next.count], 0, sizeof(report_t));
        if (save_outbox(&next))
          s_outbox = next;
        else
          sent = false;
      }
      xSemaphoreGive(s_lock);
    }
    if (sent)
      backoff_ms = 1000;
    else {
      ESP_LOGW(TAG, "Report retained; retry in %lu ms",
               (unsigned long)backoff_ms);
      vTaskDelay(pdMS_TO_TICKS(backoff_ms));
      if (backoff_ms < 60000)
        backoff_ms = backoff_ms > 30000 ? 60000 : backoff_ms * 2;
    }
  }
}

bool cloud_client_init(void) {
  if (s_initialized) return true;
#if !CONFIG_SMART_HELMET_CLOUD_ENABLE
  s_initialized = true;
  ESP_LOGW(TAG, "Cloud disabled by configuration");
  return true;
#endif
  if (nvs_flash_init() != ESP_OK ||
      nvs_open("helmet_reports", NVS_READWRITE, &s_nvs) != ESP_OK)
    return false;
  s_lock = xSemaphoreCreateMutex();
  if (!s_lock) {
    nvs_close(s_nvs);
    return false;
  }
  size_t size = sizeof(s_outbox);
  esp_err_t err = nvs_get_blob(s_nvs, "pending", &s_outbox, &size);
  if (err == ESP_ERR_NVS_NOT_FOUND)
    s_outbox = (outbox_t){.version = OUTBOX_VERSION};
  else if (err != ESP_OK || size != sizeof(s_outbox) ||
           s_outbox.version != OUTBOX_VERSION ||
           s_outbox.count > CONFIG_SMART_HELMET_CLOUD_QUEUE_LENGTH) {
    ESP_LOGE(TAG, "Outbox incompatible/corrupt; retained in NVS for recovery");
    vSemaphoreDelete(s_lock);
    s_lock = NULL;
    nvs_close(s_nvs);
    return false;
  }
  if (xTaskCreate(cloud_task, "cloud_client", 8192, NULL, 4, NULL) != pdPASS) {
    vSemaphoreDelete(s_lock);
    s_lock = NULL;
    nvs_close(s_nvs);
    return false;
  }
  s_enabled = s_initialized = true;
  if (!CONFIG_SMART_HELMET_CLOUD_URL[0])
    ESP_LOGW(TAG, "BLOCKED: endpoint empty; retaining reports");
  return true;
}

bool cloud_client_submit_emergency(
    const cloud_client_emergency_report_t *report) {
  if (!report || !s_enabled || !s_initialized) return false;
  report_t item = {.id = esp_random(),
                   .uptime_ms = report->uptime_ms,
                   .state = report->state};
  snprintf(item.source, sizeof(item.source), "%s",
           report->source_event ? report->source_event : "UNKNOWN");
  gps_get_snapshot(&item.gps);
  if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(20)) != pdTRUE) return false;
  outbox_t next = s_outbox;
  if (next.count == CONFIG_SMART_HELMET_CLOUD_QUEUE_LENGTH) {
    ESP_LOGW(TAG, "Outbox full: replace oldest with newest emergency");
    --next.count;
    memmove(next.items, next.items + 1, next.count * sizeof(report_t));
  }
  next.items[next.count++] = item;
  bool saved = save_outbox(&next);
  if (saved) s_outbox = next;
  xSemaphoreGive(s_lock);
  return saved;
}

bool cloud_client_is_enabled(void) { return s_enabled; }
