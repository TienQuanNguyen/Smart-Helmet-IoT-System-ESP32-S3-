#include "hardware_test_internal.h"
#include "power_manager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "system_state.h"

static const char *TAG = "test_phase12";
static void monitor(void *arg) {
    (void)arg;
    for (;;) {
        power_snapshot_t s;
        if (power_manager_get_snapshot(&s))
            ESP_LOGI(TAG, "source=%s status=%s voltage_valid=%d voltage=%.3fV soc_valid=%d runtime_valid=%d age=%lu",
                power_source_name(s.source),power_status_name(s.status),s.voltage_valid,
                (double)s.voltage_v,s.soc_valid,s.runtime_valid,(unsigned long)s.age_ms);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
bool phase12_power_test_start(void) {
    unsigned pass=0,fail=0;
#define CHECK(x) do { if (x) ++pass; else { ++fail; ESP_LOGE(TAG,"FAIL line %d",__LINE__); } } while (0)
    ESP_LOGW(TAG,"PHASE12 MOCK ONLY: synthetic samples, not battery measurements");
    if (!event_manager_init() || !system_state_init()) return false;
    power_config_t c=power_manager_default_config(); c.source=POWER_MOCK;
    power_model_t model={0}; power_snapshot_t s;
    power_sample_t raw={.voltage_valid=true,.voltage_v=3.75f};
    power_model_update(&model,&c,&raw,true,0,&s);
    CHECK(s.voltage_valid && !s.soc_valid && !s.runtime_valid);
    /* Explicit synthetic gauge to exercise alert path, not the default fixture. */
    c.measurement_verified=true;
    raw.gauge_soc_valid=true; raw.gauge_soc_pct=5;
    for (unsigned i=0;i<c.confirm_samples;++i) {
        raw.timestamp_ms=(i+1)*c.period_ms;
        power_model_update(&model,&c,&raw,true,raw.timestamp_ms,&s);
    }
    CHECK(power_model_pending_event(&model,&s)==SYSTEM_EVENT_BATTERY_CRITICAL);
    system_event_t event=SYSTEM_EVENT_NONE;
    CHECK(event_manager_publish(power_model_pending_event(&model,&s)));
    CHECK(event_manager_wait(&event,100) && event==SYSTEM_EVENT_BATTERY_CRITICAL);
    power_model_event_delivered(&model);
    CHECK(power_model_pending_event(&model,&s)==SYSTEM_EVENT_NONE);
    system_state_set(SYSTEM_STATE_DRIVING_MONITORING);
    system_state_handle_event(event);
    CHECK(system_state_get()==SYSTEM_STATE_DRIVING_MONITORING);
    system_state_handle_event(SYSTEM_EVENT_IMU_IMPACT);
    system_state_handle_event(event);
    CHECK(system_state_get()==SYSTEM_STATE_EMERGENCY_REPORTING);
    system_state_set(SYSTEM_STATE_DRIVING_MONITORING);
    system_state_handle_event(SYSTEM_EVENT_BATTERY_LOW);
    system_state_handle_event(SYSTEM_EVENT_SOS_PRESSED);
    CHECK(system_state_get()==SYSTEM_STATE_EMERGENCY_REPORTING);
    power_snapshot_expire(&s,raw.timestamp_ms+c.stale_ms+1,c.stale_ms);
    CHECK(s.status==POWER_STALE && !s.soc_valid);
    c=power_manager_default_config(); c.source=POWER_MOCK;
    CHECK(power_manager_init(&c)); CHECK(power_manager_start());
    ESP_LOGI(TAG,"MOCK CHECKS: pass=%u fail=%u; HARDWARE BATTERY: BLOCKED",pass,fail);
#undef CHECK
    return fail==0 && xTaskCreate(monitor,"power_test",3072,NULL,1,NULL)==pdPASS;
}
