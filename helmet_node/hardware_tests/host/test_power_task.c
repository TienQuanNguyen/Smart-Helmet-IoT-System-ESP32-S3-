#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include "../../components/power_manager/power_manager.c"

static unsigned checks, reads, publishes;
static bool accept, fail_read;
static int64_t clock_us;
static jmp_buf scheduler;
#define CHECK(x) do { ++checks; assert(x); } while (0)
int64_t esp_timer_get_time(void) { return clock_us; }
void vTaskDelay(unsigned ticks) {
    CHECK(ticks == 5000);
    clock_us += (int64_t)ticks*1000;
    longjmp(scheduler,1);
}
bool event_manager_publish(system_event_t event) {
    CHECK(event == SYSTEM_EVENT_BATTERY_CRITICAL);
    ++publishes;
    return accept;
}
static bool read_sample(void *context, uint32_t timeout, power_sample_t *out) {
    CHECK(context == &reads && timeout == 10);
    ++reads;
    *out=(power_sample_t){.timestamp_ms=(uint32_t)(clock_us/1000),
        .gauge_soc_valid=true,.gauge_soc_pct=5};
    return !fail_read;
}
static void tick(void) { if (!setjmp(scheduler)) power_task(NULL); }
int main(void) {
    power_snapshot_t snapshot;
    CHECK(!power_manager_start());
    CHECK(!power_manager_get_snapshot(NULL));
    CHECK(!power_manager_get_snapshot(&snapshot));
    power_config_t c=power_manager_default_config();
    c.source=POWER_EXTERNAL;
    CHECK(!power_manager_init(&c));
    c.read=read_sample; c.read_context=&reads; c.measurement_verified=true;
    CHECK(power_manager_init(&c));
    CHECK(!power_manager_init(&c));
    CHECK(power_manager_start()); CHECK(power_manager_start());
    tick(); tick(); CHECK(publishes==0);
    tick(); CHECK(publishes==1);
    tick(); CHECK(publishes==2); /* failed publish retried at next sample only */
    accept=true; tick(); CHECK(publishes==3);
    tick(); CHECK(publishes==3);
    CHECK(power_manager_get_snapshot(&snapshot));
    CHECK(snapshot.source==POWER_EXTERNAL && snapshot.soc_valid && snapshot.level==POWER_CRITICAL);
    clock_us += 16000000;
    CHECK(power_manager_get_snapshot(&snapshot));
    CHECK(snapshot.status==POWER_STALE && !snapshot.soc_valid);
    fail_read=true; tick();
    CHECK(power_manager_get_snapshot(&snapshot));
    CHECK(snapshot.status==POWER_READ_ERROR && !snapshot.soc_valid);
    power_sample_t fixture;
    CHECK(power_mock_read(NULL,10,&fixture));
    CHECK(fixture.voltage_valid && !fixture.gauge_soc_valid);
    CHECK(!power_mock_read(NULL,10,NULL));
    printf("PASS: %u power task/snapshot/retry checks (mock scheduler/backend)\n",checks);
    return 0;
}
