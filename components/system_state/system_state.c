#include "system_state.h"

#include "esp_log.h"

static const char *TAG = "system_state";

static system_state_t s_state = SYSTEM_STATE_BOOT;

static bool system_state_is_emergency(system_state_t state) {
  return (state == SYSTEM_STATE_ACCIDENT_DETECTED) ||
         (state == SYSTEM_STATE_EMERGENCY_REPORTING);
}

bool system_state_init(void) {
  s_state = SYSTEM_STATE_BOOT;
  ESP_LOGI(TAG, "Initial state: %s", system_state_to_string(s_state));
  system_state_set(SYSTEM_STATE_STARTUP_ALCOHOL_CHECK);
  return true;
}

system_state_t system_state_get(void) {
  return s_state;
}

void system_state_set(system_state_t new_state) {
  if (new_state == s_state) {
    return;
  }

  ESP_LOGI(TAG, "State transition: %s -> %s", system_state_to_string(s_state),
           system_state_to_string(new_state));
  s_state = new_state;
}

void system_state_handle_event(system_event_t event) {
  if (event == SYSTEM_EVENT_NONE) {
    return;
  }

  if (event == SYSTEM_EVENT_SOS_PRESSED) {
    system_state_set(SYSTEM_STATE_EMERGENCY_REPORTING);
    return;
  }

  if ((event == SYSTEM_EVENT_BATTERY_LOW) &&
      !system_state_is_emergency(s_state)) {
    system_state_set(SYSTEM_STATE_LOW_POWER_IDLE);
    return;
  }

  switch (s_state) {
    case SYSTEM_STATE_STARTUP_ALCOHOL_CHECK:
      if (event == SYSTEM_EVENT_ALCOHOL_PASS) {
        system_state_set(SYSTEM_STATE_READY_TO_RIDE);
      } else if (event == SYSTEM_EVENT_ALCOHOL_FAIL) {
        system_state_set(SYSTEM_STATE_LOW_POWER_IDLE);
      }
      break;

    case SYSTEM_STATE_READY_TO_RIDE:
      if (event == SYSTEM_EVENT_NONE) {
        system_state_set(SYSTEM_STATE_DRIVING_MONITORING);
      }
      break;

    case SYSTEM_STATE_DRIVING_MONITORING:
      if ((event == SYSTEM_EVENT_IMU_IMPACT) ||
          (event == SYSTEM_EVENT_FALL_CONFIRMED)) {
        system_state_set(SYSTEM_STATE_ACCIDENT_DETECTED);
      }
      break;

    case SYSTEM_STATE_ACCIDENT_DETECTED:
      system_state_set(SYSTEM_STATE_EMERGENCY_REPORTING);
      break;

    default:
      break;
  }
}

const char *system_state_to_string(system_state_t state) {
  switch (state) {
    case SYSTEM_STATE_BOOT:
      return "BOOT";
    case SYSTEM_STATE_STARTUP_ALCOHOL_CHECK:
      return "STARTUP_ALCOHOL_CHECK";
    case SYSTEM_STATE_READY_TO_RIDE:
      return "READY_TO_RIDE";
    case SYSTEM_STATE_DRIVING_MONITORING:
      return "DRIVING_MONITORING";
    case SYSTEM_STATE_ACCIDENT_DETECTED:
      return "ACCIDENT_DETECTED";
    case SYSTEM_STATE_EMERGENCY_REPORTING:
      return "EMERGENCY_REPORTING";
    case SYSTEM_STATE_LOW_POWER_IDLE:
      return "LOW_POWER_IDLE";
    default:
      return "UNKNOWN_STATE";
  }
}

const char *system_event_to_string(system_event_t event) {
  switch (event) {
    case SYSTEM_EVENT_NONE:
      return "NONE";
    case SYSTEM_EVENT_ALCOHOL_PASS:
      return "ALCOHOL_PASS";
    case SYSTEM_EVENT_ALCOHOL_FAIL:
      return "ALCOHOL_FAIL";
    case SYSTEM_EVENT_IMU_IMPACT:
      return "IMU_IMPACT";
    case SYSTEM_EVENT_FALL_CONFIRMED:
      return "FALL_CONFIRMED";
    case SYSTEM_EVENT_GPS_VALID:
      return "GPS_VALID";
    case SYSTEM_EVENT_WIFI_CONNECTED:
      return "WIFI_CONNECTED";
    case SYSTEM_EVENT_WIFI_LOST:
      return "WIFI_LOST";
    case SYSTEM_EVENT_BLE_CONNECTED:
      return "BLE_CONNECTED";
    case SYSTEM_EVENT_SOS_PRESSED:
      return "SOS_PRESSED";
    case SYSTEM_EVENT_BATTERY_LOW:
      return "BATTERY_LOW";
    default:
      return "UNKNOWN_EVENT";
  }
}
