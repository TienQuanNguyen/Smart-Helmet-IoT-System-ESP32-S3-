#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
/* Compile the actual worker; stop its loop at a mocked scheduler delay. */
#include "../../components/cloud_client/cloud_client.c"

static unsigned checks, delays, http_calls, commits;
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
                                   .fix_valid = true,
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
  return (void *)1;
}
esp_err_t esp_http_client_set_method(esp_http_client_handle_t client,
                                     int method) {
  (void)client;
  CHECK(method == HTTP_METHOD_POST);
  return ESP_OK;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t client,
                                     const char *key, const char *value) {
  (void)client;
  (void)key;
  (void)value;
  return ESP_OK;
}
esp_err_t esp_http_client_set_post_field(esp_http_client_handle_t client,
                                         const char *data, int size) {
  (void)client;
  CHECK(size == (int)strlen(data));
  cJSON *json = cJSON_Parse(data);
  CHECK(json != NULL);
  CHECK(cJSON_GetObjectItem(json, "gps") != NULL);
  CHECK(cJSON_GetObjectItem(json, "report_id") != NULL);
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
  char payload[640];
  CHECK(build_payload(&s_outbox.items[0], payload, sizeof(payload)));
  cJSON *json = cJSON_Parse(payload);
  CHECK(json != NULL);
  CHECK(strcmp(cJSON_GetObjectItem(json, "event")->valuestring,
               report.source_event) == 0);
  CHECK(cJSON_GetObjectItem(cJSON_GetObjectItem(json, "gps"), "age_ms")
            ->valueint == 100);
  cJSON_Delete(json);
  run_worker(8);
  CHECK(http_calls == 0 && s_outbox.count == 1 && last_delay == 60000);
  wifi_up = true;
  const int rejected[] = {301, 400, 500};
  for (unsigned i = 0; i < sizeof(rejected) / sizeof(rejected[0]); ++i) {
    http_status = rejected[i];
    run_worker(1);
    CHECK(s_outbox.count == 1 && durable.count == 1 && last_delay == 1000);
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
  printf("PASS: %u cloud checks (mock HTTP/NVS, no network)\n", checks);
  return 0;
}
