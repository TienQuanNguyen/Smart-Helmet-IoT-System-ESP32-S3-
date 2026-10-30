#include "power_manager.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "power_manager";
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static power_config_t s_config;
static power_snapshot_t s_snapshot;
static power_model_t s_model;
static bool s_initialized, s_started;

/* Synthetic fixture only; deliberately supplies no SOC/capacity profile. */
bool power_mock_read(void *context, uint32_t timeout_ms, power_sample_t *sample) {
    (void)context; (void)timeout_ms;
    if (!sample) return false;
    *sample = (power_sample_t){.timestamp_ms = (uint32_t)(esp_timer_get_time()/1000),
        .voltage_valid = true, .current_valid = true, .voltage_v = 3.75f,
        .current_ma = 120};
    return true;
}
static void power_task(void *arg) {
    (void)arg;
    for (;;) {
        power_sample_t raw = {0};
        power_read_fn read = s_config.source == POWER_MOCK ? power_mock_read : s_config.read;
        bool ok = read && read(s_config.read_context, s_config.read_timeout_ms, &raw);
        power_snapshot_t next;
        power_model_update(&s_model, &s_config, &raw, ok,
                           (uint32_t)(esp_timer_get_time()/1000), &next);
        portENTER_CRITICAL(&s_mux);
        s_snapshot = next;
        portEXIT_CRITICAL(&s_mux);
        system_event_t event = power_model_pending_event(&s_model, &next);
        if (event != SYSTEM_EVENT_NONE) {
            if (event_manager_publish(event)) power_model_event_delivered(&s_model);
            else ESP_LOGW(TAG, "Battery event pending; queue full");
        }
        vTaskDelay(pdMS_TO_TICKS(s_config.period_ms));
    }
}
bool power_manager_init(const power_config_t *config) {
    /* Startup-only API; immutable config after initialization. */
    if (s_initialized || !power_config_valid(config)) return false;
    s_config = *config;
    s_snapshot = (power_snapshot_t){.source = config->source,
                                    .status = POWER_UNAVAILABLE,
                                    .valid_for_ms = config->stale_ms};
    s_initialized = true;
    ESP_LOGI(TAG, "source=%s; SOC requires verified profile/gauge; sleep disabled",
             power_source_name(config->source));
    return true;
}
bool power_manager_start(void) {
    if (!s_initialized) return false;
    if (s_started || s_config.source == POWER_DISABLED) return true;
    s_started = xTaskCreate(power_task, "power_manager", 3072, NULL, 2, NULL) == pdPASS;
    return s_started;
}
bool power_manager_get_snapshot(power_snapshot_t *out) {
    if (!out) return false;
    portENTER_CRITICAL(&s_mux);
    *out = s_snapshot;
    portEXIT_CRITICAL(&s_mux);
    if (!s_initialized) return false;
    power_snapshot_expire(out, (uint32_t)(esp_timer_get_time()/1000), s_config.stale_ms);
    return true;
}
