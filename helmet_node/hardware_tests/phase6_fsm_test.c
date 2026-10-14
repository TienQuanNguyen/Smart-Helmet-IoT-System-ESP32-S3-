#include "hardware_test_internal.h"

#include "esp_log.h"
#include "event_manager.h"
#include "system_state.h"

static const char *TAG = "test_phase6";

static bool expect_state(system_state_t expected, const char *step) {
  system_state_t actual = system_state_get();
  if (actual != expected) {
    ESP_LOGE(TAG, "FAIL %s: expected=%s actual=%s", step,
             system_state_to_string(expected), system_state_to_string(actual));
    return false;
  }
  ESP_LOGI(TAG, "PASS %s: %s", step, system_state_to_string(actual));
  return true;
}

static bool send_queued_event(system_event_t event) {
  system_event_t received = SYSTEM_EVENT_NONE;
  if (!event_manager_publish(event) || !event_manager_wait(&received, 100) ||
      (received != event)) {
    ESP_LOGE(TAG, "FAIL queue event: %s", system_event_to_string(event));
    return false;
  }
  system_state_handle_event(received);
  return true;
}

static bool process_queue_timeout(const char *step) {
  system_event_t received = SYSTEM_EVENT_NONE;
  if (event_manager_wait(&received, 10)) {
    ESP_LOGE(TAG, "FAIL %s: unexpected event=%s", step,
             system_event_to_string(received));
    return false;
  }
  if (received != SYSTEM_EVENT_NONE) {
    ESP_LOGE(TAG, "FAIL %s: timeout output changed to %s", step,
             system_event_to_string(received));
    return false;
  }

  ESP_LOGI(TAG, "PASS %s: empty queue timed out", step);
  system_state_handle_event(received);
  return true;
}

bool phase6_fsm_test_start(void) {
  unsigned pass_count = 0;
  unsigned fail_count = 0;

  if (!event_manager_init() || !system_state_init()) {
    ESP_LOGE(TAG, "FAIL: manager initialization failed");
    return false;
  }

#define CHECK(condition) ((condition) ? ++pass_count : ++fail_count)

  CHECK(expect_state(SYSTEM_STATE_STARTUP_ALCOHOL_CHECK, "initial state"));
  CHECK(send_queued_event(SYSTEM_EVENT_ALCOHOL_PASS));
  CHECK(expect_state(SYSTEM_STATE_READY_TO_RIDE, "alcohol pass"));
  CHECK(process_queue_timeout("ready timeout"));
  CHECK(expect_state(SYSTEM_STATE_DRIVING_MONITORING, "queue timeout"));
  CHECK(send_queued_event(SYSTEM_EVENT_IMU_IMPACT));
  CHECK(expect_state(SYSTEM_STATE_ACCIDENT_DETECTED, "impact"));
  CHECK(process_queue_timeout("accident timeout"));
  CHECK(expect_state(SYSTEM_STATE_EMERGENCY_REPORTING, "emergency reporting"));

  system_state_init();
  CHECK(send_queued_event(SYSTEM_EVENT_ALCOHOL_FAIL));
  CHECK(expect_state(SYSTEM_STATE_LOW_POWER_IDLE, "alcohol fail"));

  system_state_init();
  CHECK(send_queued_event(SYSTEM_EVENT_SOS_PRESSED));
  CHECK(expect_state(SYSTEM_STATE_EMERGENCY_REPORTING, "SOS"));

  system_state_init();
  CHECK(send_queued_event(SYSTEM_EVENT_BATTERY_LOW));
  CHECK(expect_state(SYSTEM_STATE_LOW_POWER_IDLE, "battery low"));

#undef CHECK

  ESP_LOGI(TAG, "FSM TEST COMPLETE: pass=%u fail=%u", pass_count, fail_count);
  return fail_count == 0;
}
