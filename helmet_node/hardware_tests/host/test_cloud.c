#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
/* Compile the actual worker; stop its loop at a mocked scheduler delay. */
#include "../../components/cloud_client/cloud_client.c"

static unsigned checks, delays, http_calls, commits;
static bool gps_valid = true;
#ifdef HOST_FIREBASE_TEST
static int64_t clock_us;
int64_t esp_timer_get_time(void) { return clock_us; }
static char last_url[1200], first_event_url[1200];
static bool saw_bearer;
static unsigned telemetry_calls;
#endif
static bool wifi_up, fail_commit, nvs_present;
static esp_err_t http_error;
static int http_status = 200;
static unsigned last_delay, stop_after_delays = 1;
static outbox_t durable, staged;
static jmp_buf scheduler;
#define CHECK(expr) \
  do {              \
    ++checks;       \
    assert(expr);   \
  } while (0)

bool wifi_manager_is_connected(void) { return wifi_up; }
bool gps_get_snapshot(gps_snapshot_t *out) {
  *out = (gps_snapshot_t){.data = {.latitude = 10.8,
                                   .longitude = 106.6,
                                   .fix_valid = gps_valid,
                                   .satellites = 8,
                                   .hdop = 1.2f},
                          .age_ms = 100};
  return true;
}
uint32_t esp_random(void) {
  static uint32_t id;
  return ++id;
}
void vTaskDelay(unsigned ticks) {
  last_delay = ticks;
#ifdef HOST_FIREBASE_TEST
  clock_us += (int64_t)ticks * 1000;
#endif
  if (++delays >= stop_after_delays) longjmp(scheduler, 1);
}
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *out) {
  (void)name;
  (void)mode;
  *out = 1;
  return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *value,
                       size_t *size) {
  (void)handle;
  (void)key;
  if (!nvs_present) return ESP_ERR_NVS_NOT_FOUND;
  CHECK(*size == sizeof(durable));
  memcpy(value, &durable, sizeof(durable));
  return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value,
                       size_t size) {
  (void)handle;
  (void)key;
  CHECK(size == sizeof(staged));
  memcpy(&staged, value, size);
  return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle) {
  (void)handle;
  if (fail_commit) return ESP_FAIL;
  durable = staged;
  nvs_present = true;
  ++commits;
  return ESP_OK;
}
void nvs_close(nvs_handle_t handle) { (void)handle; }
esp_http_client_handle_t esp_http_client_init(
    const esp_http_client_config_t *config) {
  CHECK(config->disable_auto_redirect);
#ifdef HOST_FIREBASE_TEST
  CHECK(config->crt_bundle_attach != NULL);
  snprintf(last_url, sizeof(last_url), "%s", config->url);
  CHECK(strstr(last_url, ".json?auth=%74%65%73%74%20%2F%3F%26&print=silent") != NULL);
  if (strstr(last_url, "/Events/")) {
    if (!first_event_url[0]) strcpy(first_event_url, last_url);
    CHECK(strcmp(first_event_url, last_url) == 0);
  }
#endif
  return (void *)1;
}
esp_err_t esp_http_client_set_method(esp_http_client_handle_t client,
                                     int method) {
  (void)client;
#ifdef HOST_FIREBASE_TEST
  CHECK(method == HTTP_METHOD_PATCH);
#else
  CHECK(method == HTTP_METHOD_POST);
#endif
  return ESP_OK;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t client,
                                     const char *key, const char *value) {
  (void)client;
  (void)key;
  (void)value;
#ifdef HOST_FIREBASE_TEST
  if (!strcmp(key, "Authorization")) saw_bearer = true;
#endif
  return ESP_OK;
}
esp_err_t esp_http_client_set_post_field(esp_http_client_handle_t client,
                                         const char *data, int size) {
  (void)client;
  CHECK(size == (int)strlen(data));
  cJSON *json = cJSON_Parse(data);
  CHECK(json != NULL);
#ifdef HOST_FIREBASE_TEST
  if (strstr(last_url, "/telemetry.json")) {
    ++telemetry_calls;
    CHECK(cJSON_IsTrue(cJSON_GetObjectItem(json, "internet")));
    CHECK(cJSON_GetObjectItem(json, "last_update") != NULL);
    CHECK(cJSON_GetObjectItem(json, "control") == NULL);
  } else {
    CHECK(cJSON_GetObjectItem(json, "lng") != NULL);
    CHECK(cJSON_GetObjectItem(json, "timestamp") != NULL);
  }
#else
  CHECK(cJSON_GetObjectItem(json, "gps") != NULL);
  CHECK(cJSON_GetObjectItem(json, "report_id") != NULL);
#endif
  cJSON_Delete(json);
  return ESP_OK;
}
esp_err_t esp_http_client_perform(esp_http_client_handle_t client) {
  (void)client;
  ++http_calls;
  return http_error;
}
int esp_http_client_get_status_code(esp_http_client_handle_t client) {
  (void)client;
  return http_status;
}
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client) {
  (void)client;
  return ESP_OK;
}

static void run_worker(unsigned sleeps) {
  stop_after_delays = sleeps;
  delays = 0;
  if (!setjmp(scheduler)) cloud_task(NULL);
}
int main(void) {
  CHECK(cloud_client_init());
  CHECK(cloud_client_is_enabled());
  cloud_client_emergency_report_t report = {
      .source_event = "SOS_\"TEST",
      .uptime_ms = 123,
      .state = SYSTEM_STATE_EMERGENCY_REPORTING};
  CHECK(cloud_client_submit_emergency(&report));
  CHECK(s_outbox.count == 1 && durable.count == 1 && commits == 1);
  char payload[1024];
  CHECK(build_payload(&s_outbox.items[0], payload, sizeof(payload)));
  cJSON *json = cJSON_Parse(payload);
  CHECK(json != NULL);
  CHECK(strcmp(cJSON_GetObjectItem(json, "event")->valuestring,
               report.source_event) == 0);
  CHECK(cJSON_GetObjectItem(cJSON_GetObjectItem(json, "gps"), "age_ms")
            ->valueint == 100);
  cJSON_Delete(json);
  run_worker(8);
#ifdef HOST_FIREBASE_TEST
  CHECK(http_calls == 0 && s_outbox.count == 1 && last_delay == 250);
#else
  CHECK(http_calls == 0 && s_outbox.count == 1 && last_delay == 60000);
#endif
  wifi_up = true;
  const int rejected[] = {301, 400, 401, 403, 500};
  for (unsigned i = 0; i < sizeof(rejected) / sizeof(rejected[0]); ++i) {
    http_status = rejected[i];
    run_worker(1);
    CHECK(s_outbox.count == 1 && durable.count == 1);
#ifdef HOST_FIREBASE_TEST
    CHECK(last_delay == 250);
#else
    CHECK(last_delay == 1000);
#endif
  }
  http_status = 200;
  http_error = ESP_FAIL;
  run_worker(1);
  CHECK(s_outbox.count == 1 && durable.count == 1);
  http_error = ESP_OK;
  fail_commit = true;
  run_worker(1);
  CHECK(s_outbox.count == 1 && durable.count == 1);
  fail_commit = false;
  http_status = 204;
  run_worker(1);
  CHECK(s_outbox.count == 0 && durable.count == 0);
  CHECK(last_delay == 250);
  for (unsigned i = 0; i < 6; ++i)
    CHECK(cloud_client_submit_emergency(&report));
  CHECK(s_outbox.count == 4 && durable.count == 4);
  CHECK(s_outbox.items[0].id == 4 && s_outbox.items[3].id == 7);
  uint32_t retained_id = s_outbox.items[0].id;
  fail_commit = true;
  CHECK(!cloud_client_submit_emergency(&report));
  CHECK(s_outbox.items[0].id == retained_id &&
        durable.items[0].id == retained_id);
  fail_commit = false;
  s_initialized = s_enabled = false;
  memset(&s_outbox, 0, sizeof(s_outbox));
  CHECK(cloud_client_init());
  CHECK(s_outbox.count == 4 && s_outbox.items[0].id == retained_id);
#ifdef HOST_FIREBASE_TEST
  CHECK(!saw_bearer);
  char url[1200];
  CHECK(firebase_url("Devices/host-test/telemetry", url, sizeof(url)));
  CHECK(strstr(url, "/Devices/host-test/telemetry.json?") != NULL);
  CHECK(!firebase_url("Devices/host-test/telemetry", url, 12));
  cloud_client_telemetry_t sample = {
    .state = SYSTEM_STATE_DRIVING_MONITORING, .uptime_ms = 1234,
    .imu_valid = true, .accel_mag_g = 1.1f,
    .alcohol_valid = true, .alcohol_raw = 321, .alcohol_voltage = 0.3f};
  CHECK(cloud_client_update_telemetry(&sample));
  CHECK(!cloud_client_update_telemetry(NULL));
  sample.accel_mag_g = NAN;
  CHECK(!cloud_client_update_telemetry(&sample));
  CHECK(build_telemetry(payload, sizeof(payload)));
  json = cJSON_Parse(payload);
  CHECK(cJSON_GetObjectItem(json, "alcohol_raw")->valueint == 321);
  CHECK(cJSON_GetObjectItem(json, "speed") == NULL);
  CHECK(cJSON_GetObjectItem(json, "lock") == NULL);
  CHECK(cJSON_GetObjectItem(json, "alcohol_level") == NULL);
  CHECK(strcmp(cJSON_GetObjectItem(cJSON_GetObjectItem(json, "last_update"), ".sv")->valuestring, "timestamp") == 0);
  cJSON_Delete(json);
  gps_valid = false;
  sample.imu_valid = sample.alcohol_valid = false;
  CHECK(cloud_client_update_telemetry(&sample));
  CHECK(build_telemetry(payload, sizeof(payload)));
  json = cJSON_Parse(payload);
  CHECK(cJSON_IsFalse(cJSON_GetObjectItem(json, "gps_fix")));
  CHECK(cJSON_GetObjectItem(json, "lat")->valuedouble == 0);
  CHECK(cJSON_GetObjectItem(json, "lng")->valuedouble == 0);
  CHECK(cJSON_IsNull(cJSON_GetObjectItem(json, "accel_mag_g")));
  CHECK(cJSON_IsNull(cJSON_GetObjectItem(json, "alcohol_detected")));
  cJSON_Delete(json);
  s_next_telemetry_us = 0;
  service_telemetry();
  CHECK(telemetry_calls == 1);
  service_telemetry();
  CHECK(telemetry_calls == 1);
  clock_us += 5000000;
  service_telemetry();
  CHECK(telemetry_calls == 2);
  wifi_up = false;
  clock_us += 5000000;
  service_telemetry();
  CHECK(telemetry_calls == 2);
  wifi_up = true;
  first_event_url[0] = 0;
  http_status = 403;
  /* Telemetry is still serviced during emergency backoff. */
  run_worker(1);
  CHECK(telemetry_calls == 3 && s_outbox.count == 4);
#endif
  s_initialized = s_enabled = false;
  durable.version = 999;
  CHECK(!cloud_client_init());
  CHECK(durable.count == 4 && durable.version == 999);
  printf("PASS: %u cloud checks (mock HTTP/NVS, no network)\n", checks);
  return 0;
}
