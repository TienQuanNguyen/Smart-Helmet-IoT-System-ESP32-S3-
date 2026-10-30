#include "cloud_client.h"
#include "power_json.h"

#include <stdio.h>
#include <math.h>
#include <time.h>
#include "esp_timer.h"
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

#ifndef CONFIG_SMART_HELMET_CLOUD_FIREBASE
#define CONFIG_SMART_HELMET_CLOUD_FIREBASE 0
#endif
#ifndef CONFIG_SMART_HELMET_CLOUD_TELEMETRY_INTERVAL_MS
#define CONFIG_SMART_HELMET_CLOUD_TELEMETRY_INTERVAL_MS 5000
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
static cloud_client_telemetry_t s_telemetry;
static bool s_telemetry_present;
static int64_t s_next_telemetry_us;

/* Restrict IDs to a single path segment; reject URLs with credentials/query. */
static bool firebase_config_valid(void) {
  const char *url = CONFIG_SMART_HELMET_CLOUD_URL;
  const char *id = CONFIG_SMART_HELMET_DEVICE_ID;
  if (strncmp(url, "https://", 8) || !url[8] || !id[0] ||
      !CONFIG_SMART_HELMET_CLOUD_AUTH_TOKEN[0]) return false;
  const char *host = url + 8;
  size_t n = strcspn(host, "/");
  if (!n || (host[n] && strcmp(host + n, "/")) ||
      strpbrk(host, "?#@\\ \r\n")) return false;
  const char *suffixes[] = {".firebaseio.com", ".firebasedatabase.app"};
  bool trusted = false;
  for (size_t i = 0; i < 2; ++i) {
    size_t len = strlen(suffixes[i]);
    if (n > len && !strncmp(host + n - len, suffixes[i], len)) trusted = true;
  }
  for (; *id; ++id)
    if (!((*id >= 'A' && *id <= 'Z') || (*id >= 'a' && *id <= 'z') ||
          (*id >= '0' && *id <= '9') || *id == '_' || *id == '-')) return false;
  return trusted && strlen(CONFIG_SMART_HELMET_DEVICE_ID) <= 64;
}

static bool firebase_url(const char *path, char *out, size_t size) {
  if (!firebase_config_valid()) return false;
  char encoded[768];
  const unsigned char *token = (const unsigned char *)CONFIG_SMART_HELMET_CLOUD_AUTH_TOKEN;
  size_t used = 0;
  for (; *token; ++token) {
    if (used + 3 >= sizeof(encoded)) return false;
    int n = snprintf(encoded + used, sizeof(encoded) - used, "%%%02X", *token);
    if (n != 3) return false;
    used += 3;
  }
  encoded[used] = 0;
  size_t base = strlen(CONFIG_SMART_HELMET_CLOUD_URL);
  if (CONFIG_SMART_HELMET_CLOUD_URL[base - 1] == '/') --base;
  int n = snprintf(out, size, "%.*s/%s.json?auth=%s&print=silent",
                   (int)base, CONFIG_SMART_HELMET_CLOUD_URL, path, encoded);
  return n > 0 && (size_t)n < size;
}

static bool add_server_time(cJSON *root, const char *key) {
  cJSON *value = cJSON_AddObjectToObject(root, key);
  return value && cJSON_AddStringToObject(value, ".sv", "timestamp");
}


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
  if (ok && CONFIG_SMART_HELMET_CLOUD_FIREBASE) {
    bool sos = strstr(report->source, "SOS") != NULL;
    ok = cJSON_AddStringToObject(root, "type", sos ? "SOS" : "Phát hiện té ngã / va đập") &&
         cJSON_AddStringToObject(root, "sensor", sos ? "SOS button" : "MPU6050") &&
         cJSON_AddStringToObject(root, "severity", sos ? "critical" : "high") &&
         cJSON_AddStringToObject(root, "note", report->source) &&
         cJSON_AddBoolToObject(root, "gps_fix", gps->fix_valid) &&
         cJSON_AddNumberToObject(root, "lat", gps->fix_valid ? gps->latitude : 0) &&
         cJSON_AddNumberToObject(root, "lng", gps->fix_valid ? gps->longitude : 0) &&
         add_server_time(root, "timestamp");
  }
  if (ok) ok = cJSON_PrintPreallocated(root, payload, (int)size, false);
  cJSON_Delete(root);
  return ok;
}

static bool send_payload(const char *payload, const char *path) {
  if (CONFIG_SMART_HELMET_CLOUD_FIREBASE && time(NULL) < 1704067200) {
    ESP_LOGW(TAG, "Waiting for SNTP clock before Firebase TLS");
    return false;
  }
  char url[1200];
  if (CONFIG_SMART_HELMET_CLOUD_FIREBASE && !firebase_url(path, url, sizeof(url))) return false;
  esp_http_client_config_t config = {
      .url = CONFIG_SMART_HELMET_CLOUD_FIREBASE ? url : CONFIG_SMART_HELMET_CLOUD_URL,
      .timeout_ms = CONFIG_SMART_HELMET_CLOUD_TIMEOUT_MS,
      .crt_bundle_attach = esp_crt_bundle_attach,
      .disable_auto_redirect = true};
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) return false;
  esp_err_t err = esp_http_client_set_method(client, CONFIG_SMART_HELMET_CLOUD_FIREBASE ? HTTP_METHOD_PATCH : HTTP_METHOD_POST);
  if (err == ESP_OK)
    err =
        esp_http_client_set_header(client, "Content-Type", "application/json");
  char auth[256];
  if (err == ESP_OK && !CONFIG_SMART_HELMET_CLOUD_FIREBASE && CONFIG_SMART_HELMET_CLOUD_AUTH_TOKEN[0]) {
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
    ESP_LOGI(TAG, "Cloud write delivered, status=%d", status);
    return true;
  }
  ESP_LOGW(TAG, "HTTP delivery failed: error=%s status=%d",
           esp_err_to_name(err), status);
  return false;
}

static bool build_telemetry(char *payload, size_t size) {
  cloud_client_telemetry_t sample;
  xSemaphoreTake(s_lock, portMAX_DELAY);
  sample = s_telemetry;
  bool present = s_telemetry_present;
  xSemaphoreGive(s_lock);
  if (!present) return false;
  power_snapshot_expire(&sample.power, (uint32_t)(esp_timer_get_time()/1000), sample.power.valid_for_ms);
  gps_snapshot_t gps = {0};
  gps_get_snapshot(&gps);
  cJSON *root = cJSON_CreateObject();
  if (!root) return false;
  bool ok = cJSON_AddStringToObject(root, "state", system_state_to_string(sample.state)) &&
      cJSON_AddNumberToObject(root, "uptime_ms", sample.uptime_ms) &&
      cJSON_AddBoolToObject(root, "internet", true) &&
      cJSON_AddBoolToObject(root, "gps_fix", gps.data.fix_valid) &&
      cJSON_AddNumberToObject(root, "lat", gps.data.fix_valid ? gps.data.latitude : 0) &&
      cJSON_AddNumberToObject(root, "lng", gps.data.fix_valid ? gps.data.longitude : 0) &&
      cJSON_AddNumberToObject(root, "satellites", gps.data.satellites) &&
      cJSON_AddNumberToObject(root, "hdop", gps.data.hdop) &&
      cJSON_AddNumberToObject(root, "gps_age_ms", gps.age_ms) &&
      cJSON_AddBoolToObject(root, "is_falling", sample.fall_detected) &&
      cJSON_AddBoolToObject(root, "is_sos_pressed", sample.sos_latched) &&
      cJSON_AddBoolToObject(root, "imu_valid", sample.imu_valid) &&
      cJSON_AddBoolToObject(root, "alcohol_valid", sample.alcohol_valid) &&
      add_server_time(root, "last_update");
  if (ok) ok = sample.imu_valid ?
      cJSON_AddNumberToObject(root, "accel_mag_g", sample.accel_mag_g) != NULL :
      cJSON_AddNullToObject(root, "accel_mag_g") != NULL;
  if (ok && sample.alcohol_valid) {
    ok = cJSON_AddNumberToObject(root, "alcohol_raw", sample.alcohol_raw) &&
         cJSON_AddNumberToObject(root, "alcohol_voltage", sample.alcohol_voltage) &&
         cJSON_AddBoolToObject(root, "alcohol_detected", sample.alcohol_detected);
  } else if (ok) {
    ok = cJSON_AddNullToObject(root, "alcohol_raw") &&
         cJSON_AddNullToObject(root, "alcohol_voltage") &&
         cJSON_AddNullToObject(root, "alcohol_detected");
  }
  if (ok) ok = power_json_add(root, &sample.power);
  if (ok) ok = cJSON_PrintPreallocated(root, payload, (int)size, false);
  cJSON_Delete(root);
  return ok;
}

static void service_telemetry(void) {
  if (!CONFIG_SMART_HELMET_CLOUD_FIREBASE || !wifi_manager_is_connected()) return;
  int64_t now = esp_timer_get_time();
  if (now < s_next_telemetry_us) return;
  char payload[2048], path[128];
  snprintf(path, sizeof(path), "Devices/%s/telemetry", CONFIG_SMART_HELMET_DEVICE_ID);
  if (build_telemetry(payload, sizeof(payload))) send_payload(payload, path);
  /* Only routine telemetry slows down. The emergency worker has its own
   * schedule and BLE/IMU/SOS cadence is unchanged. Never act on mock SOC. */
  xSemaphoreTake(s_lock, portMAX_DELAY);
  power_snapshot_t power = s_telemetry.power;
  system_state_t state = s_telemetry.state;
  xSemaphoreGive(s_lock);
  power_snapshot_expire(&power, (uint32_t)(esp_timer_get_time()/1000), power.valid_for_ms);
  bool conserve = power.source == POWER_EXTERNAL && power.status == POWER_OK &&
      power.soc_valid && (power.level == POWER_LOW || power.level == POWER_CRITICAL) &&
      state != SYSTEM_STATE_ACCIDENT_DETECTED && state != SYSTEM_STATE_EMERGENCY_REPORTING;
  s_next_telemetry_us = esp_timer_get_time() +
      (int64_t)CONFIG_SMART_HELMET_CLOUD_TELEMETRY_INTERVAL_MS * (conserve ? 3000 : 1000);
}

/* Backoff does not starve telemetry; no HTTP runs on the safety/FSM task. */
static void cloud_wait(uint32_t milliseconds) {
  if (!CONFIG_SMART_HELMET_CLOUD_FIREBASE) {
    vTaskDelay(pdMS_TO_TICKS(milliseconds));
    return;
  }
  while (milliseconds) {
    service_telemetry();
    uint32_t slice = milliseconds > 250 ? 250 : milliseconds;
    vTaskDelay(pdMS_TO_TICKS(slice));
    milliseconds -= slice;
  }
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
      cloud_wait(250);
      continue;
    }
    char payload[1024], path[160];
    snprintf(path, sizeof(path), "Events/%s/esp_%08lx_%08lx",
             CONFIG_SMART_HELMET_DEVICE_ID, (unsigned long)report.id,
             (unsigned long)report.uptime_ms);
    bool sent = CONFIG_SMART_HELMET_CLOUD_URL[0] &&
                wifi_manager_is_connected() &&
                build_payload(&report, payload, sizeof(payload)) &&
                send_payload(payload, path);
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
      cloud_wait(backoff_ms);
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
  if (CONFIG_SMART_HELMET_CLOUD_FIREBASE && !firebase_config_valid()) {
    ESP_LOGE(TAG, "BLOCKED: configure Firebase HTTPS root, device ID and database secret");
    return false;
  }
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
  for (uint32_t i = 0; i < s_outbox.count; ++i) {
    if (!memchr(s_outbox.items[i].source, 0, sizeof(s_outbox.items[i].source))) {
      ESP_LOGE(TAG, "Outbox source corrupt; retained for recovery");
      vSemaphoreDelete(s_lock);
      s_lock = NULL;
      nvs_close(s_nvs);
      return false;
    }
  }
  if (xTaskCreate(cloud_task, "cloud_client", 12288, NULL, 4, NULL) != pdPASS) {
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

bool cloud_client_update_telemetry(const cloud_client_telemetry_t *sample) {
  if (!sample || !s_enabled || !s_initialized ||
      (sample->imu_valid && !isfinite(sample->accel_mag_g)) ||
      (sample->alcohol_valid && !isfinite(sample->alcohol_voltage))) return false;
  if (xSemaphoreTake(s_lock, 0) != pdTRUE) return false;
  s_telemetry = *sample;
  s_telemetry_present = true;
  xSemaphoreGive(s_lock);
  return true;
}
