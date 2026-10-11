#include "ble_comm.h"

#include <string.h>

#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "ble_comm";

static bool s_initialized;
static bool s_enabled;
static uint8_t s_sequence;
static ble_helmet_status_packet_t s_last_status;

static uint8_t ble_comm_state_to_protocol(system_state_t state) {
  switch (state) {
    case SYSTEM_STATE_BOOT:
      return BLE_HELMET_STATE_BOOT;
    case SYSTEM_STATE_STARTUP_ALCOHOL_CHECK:
      return BLE_HELMET_STATE_ALCOHOL_CHECK;
    case SYSTEM_STATE_READY_TO_RIDE:
      return BLE_HELMET_STATE_READY;
    case SYSTEM_STATE_DRIVING_MONITORING:
      return BLE_HELMET_STATE_DRIVING;
    case SYSTEM_STATE_ACCIDENT_DETECTED:
      return BLE_HELMET_STATE_ACCIDENT;
    case SYSTEM_STATE_EMERGENCY_REPORTING:
      return BLE_HELMET_STATE_EMERGENCY;
    case SYSTEM_STATE_LOW_POWER_IDLE:
      return BLE_HELMET_STATE_LOW_POWER;
    default:
      return BLE_HELMET_STATE_BOOT;
  }
}

static uint8_t ble_comm_flags_for_state(system_state_t state) {
  uint8_t flags = 0;

  if ((state == SYSTEM_STATE_READY_TO_RIDE) ||
      (state == SYSTEM_STATE_DRIVING_MONITORING)) {
    flags |= BLE_HELMET_FLAG_ALCOHOL_PASS;
  }

  if ((state == SYSTEM_STATE_ACCIDENT_DETECTED) ||
      (state == SYSTEM_STATE_EMERGENCY_REPORTING)) {
    flags |= BLE_HELMET_FLAG_EMERGENCY;
  }

  return flags;
}

bool ble_comm_init(void) {
  if (s_initialized) {
    return true;
  }

#if CONFIG_SMART_HELMET_BLE_ENABLE && CONFIG_BT_ENABLED
  s_enabled = true;
  ESP_LOGW(TAG,
           "Helmet BLE GATT server contract enabled; full NimBLE runtime is "
           "pending Bike Node integration");
#else
  s_enabled = false;
  ESP_LOGW(TAG,
           "Helmet/Bike BLE GATT disabled by project config; Bike relay path "
           "is unavailable");
#endif

  memset(&s_last_status, 0, sizeof(s_last_status));
  s_initialized = true;
  return true;
}

bool ble_comm_is_enabled(void) {
  return s_enabled;
}

bool ble_comm_notify_state(system_state_t state, uint32_t uptime_ms) {
  s_last_status = ble_comm_make_status_packet(state, uptime_ms);

  if (!s_initialized || !s_enabled) {
    return true;
  }

  ESP_LOGI(TAG, "BLE Helmet status notify: state=%s seq=%u flags=0x%02X",
           system_state_to_string(state), s_last_status.sequence,
           s_last_status.flags);
  return true;
}

bool ble_comm_send_emergency(const char *source_event, uint32_t uptime_ms) {
  s_last_status = ble_comm_make_status_packet(SYSTEM_STATE_EMERGENCY_REPORTING,
                                              uptime_ms);

  if (!s_initialized || !s_enabled) {
    return true;
  }

  ESP_LOGW(TAG, "BLE Helmet emergency notify: event=%s seq=%u",
           (source_event != NULL) ? source_event : "UNKNOWN",
           s_last_status.sequence);
  return true;
}

ble_helmet_status_packet_t ble_comm_make_status_packet(system_state_t state,
                                                       uint32_t uptime_ms) {
  ble_helmet_status_packet_t packet = {
      .version = BLE_PROTOCOL_VERSION,
      .state = ble_comm_state_to_protocol(state),
      .flags = ble_comm_flags_for_state(state),
      .sequence = ++s_sequence,
      .uptime_ms = uptime_ms,
  };

  return packet;
}
