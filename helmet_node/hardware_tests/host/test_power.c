#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "power_manager.h"
#include "power_json.h"
#include "system_state.h"

static unsigned checks;
#define CHECK(x) do { ++checks; assert(x); } while (0)
static power_sample_t sample;
static uint32_t now;
static void update(power_model_t *m, power_config_t *c, power_snapshot_t *s) {
    now += c->period_ms;
    sample.timestamp_ms = now;
    power_model_update(m, c, &sample, true, now, s);
}
static void repeat(power_model_t *m, power_config_t *c, power_snapshot_t *s) {
    for (unsigned i=0; i<c->confirm_samples; ++i) update(m,c,s);
}
int main(void) {
    power_config_t c = power_manager_default_config();
    power_model_t m = {0}; power_snapshot_t s;
    CHECK(power_config_valid(&c));
    power_model_update(&m,&c,NULL,false,0,&s);
    CHECK(s.status == POWER_UNAVAILABLE && !s.soc_valid);
    c.source = POWER_EXTERNAL;
    CHECK(!power_config_valid(&c)); /* no invented real backend */
    c.source = POWER_MOCK;
    sample = (power_sample_t){.voltage_valid=true,.voltage_v=3.75f,
                              .current_valid=true,.current_ma=100};
    repeat(&m,&c,&s);
    CHECK(s.source == POWER_MOCK && s.voltage_valid && !s.soc_valid && !s.runtime_valid);
    CHECK(power_model_pending_event(&m,&s) == SYSTEM_EVENT_NONE);
    c.measurement_verified = true;
    c.curve_verified = true; c.curve_count = 3;
    /* Synthetic test calibration only. Never shipped as a pack profile. */
    c.curve[0]=(power_curve_point_t){3.0f,0};
    c.curve[1]=(power_curve_point_t){3.6f,50};
    c.curve[2]=(power_curve_point_t){4.2f,100};
    c.capacity_verified = c.battery_side_current = true;
    c.usable_capacity_mah=1000;
    sample.voltage_v=3.6f;
    repeat(&m,&c,&s);
    CHECK(s.soc_valid && fabsf(s.soc_pct-50)<0.01f);
    CHECK(s.runtime_valid && s.remaining_minutes==300);
    sample.voltage_v=2.9f; update(&m,&c,&s);
    CHECK(!s.soc_valid && !s.runtime_valid);
    sample.voltage_v=4.3f; update(&m,&c,&s);
    CHECK(!s.soc_valid);
    sample.voltage_v=3.0f; update(&m,&c,&s); CHECK(s.soc_valid && s.soc_pct==0);
    sample.voltage_v=4.2f; update(&m,&c,&s); CHECK(s.soc_valid && s.soc_pct==100);
    sample.gauge_soc_valid=true; sample.gauge_soc_pct=19;
    m=(power_model_t){0};
    update(&m,&c,&s);
    for (unsigned i=0;i<5;++i) power_model_update(&m,&c,&sample,true,now,&s);
    CHECK(s.level==POWER_UNKNOWN); /* cached sample cannot satisfy debounce */
    update(&m,&c,&s); update(&m,&c,&s);
    CHECK(s.level==POWER_LOW);
    CHECK(power_model_pending_event(&m,&s)==SYSTEM_EVENT_BATTERY_LOW);
    /* Simulated full event queue: do not ack, event remains pending. */
    update(&m,&c,&s);
    CHECK(power_model_pending_event(&m,&s)==SYSTEM_EVENT_BATTERY_LOW);
    power_model_event_delivered(&m);
    CHECK(power_model_pending_event(&m,&s)==SYSTEM_EVENT_NONE);
    sample.gauge_soc_pct=21; repeat(&m,&c,&s); CHECK(s.level==POWER_LOW);
    sample.gauge_soc_pct=8; repeat(&m,&c,&s);
    CHECK(power_model_pending_event(&m,&s)==SYSTEM_EVENT_BATTERY_CRITICAL);
    power_model_event_delivered(&m);
    sample.gauge_soc_pct=12; repeat(&m,&c,&s); CHECK(s.level==POWER_CRITICAL);
    sample.gauge_soc_pct=25; repeat(&m,&c,&s);
    CHECK(power_model_pending_event(&m,&s)==SYSTEM_EVENT_BATTERY_RECOVERED);
    power_model_event_delivered(&m);
    CHECK(power_model_pending_event(&m,&s)==SYSTEM_EVENT_NONE);
    m=(power_model_t){0}; sample.gauge_soc_pct=5; repeat(&m,&c,&s);
    CHECK(power_model_pending_event(&m,&s)==SYSTEM_EVENT_BATTERY_CRITICAL);
    power_model_update(&m,&c,&sample,false,now,&s);
    CHECK(s.status==POWER_READ_ERROR && !s.voltage_valid && !s.soc_valid);
    CHECK(power_model_pending_event(&m,&s)==SYSTEM_EVENT_NONE);
    sample.gauge_soc_pct=NAN; update(&m,&c,&s); CHECK(!s.soc_valid);
    sample.gauge_soc_pct=101; update(&m,&c,&s); CHECK(!s.soc_valid);
    sample.gauge_soc_pct=50; sample.current_ma=-100; update(&m,&c,&s);
    CHECK(s.soc_valid && !s.runtime_valid);
    sample.current_ma=0; update(&m,&c,&s); CHECK(!s.runtime_valid);
    sample.current_ma=INFINITY; update(&m,&c,&s); CHECK(!s.current_valid && !s.runtime_valid);
    sample.current_ma=100; update(&m,&c,&s); CHECK(!s.runtime_valid);
    repeat(&m,&c,&s); CHECK(s.runtime_valid);
    c.capacity_verified=false; update(&m,&c,&s); CHECK(!s.runtime_valid);
    c.capacity_verified=true; c.battery_side_current=false; repeat(&m,&c,&s); CHECK(!s.runtime_valid);
    c.battery_side_current=true;
    power_model_update(&m,&c,&sample,true,now+c.stale_ms+1,&s);
    CHECK(s.status==POWER_STALE && !s.soc_valid);
    repeat(&m,&c,&s);
    power_snapshot_expire(&s,now+c.stale_ms+1,c.stale_ms);
    CHECK(s.status==POWER_STALE && !s.soc_valid && !s.runtime_valid);
    repeat(&m,&c,&s);
    sample.timestamp_ms=now-c.period_ms;
    power_model_update(&m,&c,&sample,true,now,&s);
    CHECK(s.status==POWER_READ_ERROR && !s.soc_valid);
    CHECK(m.last_sample_ms==now);
    m=(power_model_t){0};
    sample.timestamp_ms=UINT32_MAX-10;
    power_model_update(&m,&c,&sample,true,10,&s);
    CHECK(s.age_ms==21 && s.status==POWER_OK); /* uptime wrap */
    sample.timestamp_ms=10;
    power_model_update(&m,&c,&sample,true,10,&s);
    CHECK(m.last_sample_ms==10 && s.status==POWER_OK);
    float volts=0;
    CHECK(power_adc_divider_voltage(1000,30000,10000,2500,&volts) && volts==4);
    CHECK(!power_adc_divider_voltage(3000,30000,10000,2500,&volts));
    CHECK(!power_adc_divider_voltage(NAN,30000,10000,2500,&volts));
    CHECK(!power_adc_divider_voltage(1000,30000,0,2500,&volts));
    c.curve[1].voltage_v=2; CHECK(!power_config_valid(&c));
    c=power_manager_default_config(); c.low_pct=NAN; CHECK(!power_config_valid(&c));
    c=power_manager_default_config(); c.read_timeout_ms=1000; CHECK(!power_config_valid(&c));

    s=(power_snapshot_t){.source=POWER_MOCK,.status=POWER_OK,.voltage_valid=true,.voltage_v=3.75f};
    cJSON *root=cJSON_CreateObject(); CHECK(power_json_add(root,&s));
    cJSON *p=cJSON_GetObjectItem(root,"power");
    CHECK(cJSON_IsTrue(cJSON_GetObjectItem(p,"mock")));
    CHECK(cJSON_IsNull(cJSON_GetObjectItem(p,"soc_pct")));
    CHECK(cJSON_IsNull(cJSON_GetObjectItem(p,"remaining_minutes")));
    char tiny[10]; CHECK(!cJSON_PrintPreallocated(root,tiny,sizeof(tiny),false));
    cJSON_Delete(root);
    s.soc_valid=true; s.soc_pct=NAN; s.runtime_valid=true;
    root=cJSON_CreateObject(); CHECK(power_json_add(root,&s)); p=cJSON_GetObjectItem(root,"power");
    CHECK(cJSON_IsFalse(cJSON_GetObjectItem(p,"soc_valid")));
    CHECK(cJSON_IsNull(cJSON_GetObjectItem(p,"remaining_minutes"))); cJSON_Delete(root);
    s.status=POWER_STALE; s.soc_pct=40;
    root=cJSON_CreateObject(); CHECK(power_json_add(root,&s)); p=cJSON_GetObjectItem(root,"power");
    CHECK(cJSON_IsNull(cJSON_GetObjectItem(p,"voltage_v")));
    CHECK(cJSON_IsNull(cJSON_GetObjectItem(p,"soc_pct"))); cJSON_Delete(root);

    const system_event_t warnings[]={SYSTEM_EVENT_BATTERY_LOW,SYSTEM_EVENT_BATTERY_CRITICAL,SYSTEM_EVENT_BATTERY_RECOVERED};
    for (unsigned i=0;i<3;++i) {
        system_state_init();
        system_state_handle_event(warnings[i]);
        CHECK(system_state_get()==SYSTEM_STATE_STARTUP_ALCOHOL_CHECK);
        system_state_handle_event(SYSTEM_EVENT_ALCOHOL_PASS);
        system_state_handle_event(SYSTEM_EVENT_NONE);
        system_state_handle_event(warnings[i]);
        CHECK(system_state_get()==SYSTEM_STATE_DRIVING_MONITORING);
        system_state_handle_event(SYSTEM_EVENT_IMU_IMPACT);
        CHECK(system_state_get()==SYSTEM_STATE_ACCIDENT_DETECTED);
        system_state_handle_event(warnings[i]);
        CHECK(system_state_get()==SYSTEM_STATE_EMERGENCY_REPORTING);
        system_state_handle_event(warnings[i]);
        CHECK(system_state_get()==SYSTEM_STATE_EMERGENCY_REPORTING);
        system_state_set(SYSTEM_STATE_DRIVING_MONITORING);
        system_state_handle_event(warnings[i]);
        system_state_handle_event(SYSTEM_EVENT_FALL_CONFIRMED);
        CHECK(system_state_get()==SYSTEM_STATE_ACCIDENT_DETECTED);
        for (int state=SYSTEM_STATE_BOOT;state<=SYSTEM_STATE_LOW_POWER_IDLE;++state) {
            system_state_set((system_state_t)state);
            system_state_handle_event(warnings[i]);
            system_state_handle_event(SYSTEM_EVENT_SOS_PRESSED);
            CHECK(system_state_get()==SYSTEM_STATE_EMERGENCY_REPORTING);
        }
    }
    printf("PASS: %u power/model/JSON/safety checks (synthetic host data only)\n",checks);
    return 0;
}
