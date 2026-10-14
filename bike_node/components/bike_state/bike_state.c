#include "bike_state.h"

#include <string.h>

#include "esp_log.h"

static const char *TAG = "bike_state";

typedef struct {
  bool rfid_authorized;
  bool helmet_connected;
  bool helmet_ready;
  bool alcohol_pass;
  bike_state_status_t status;
} bike_state_context_t;

static bike_state_context_t s_ctx;

static void bike_state_refresh_status(void) {
  const bool allowed = bike_state_is_ignition_allowed();

  if (allowed) {
    s_ctx.status = BIKE_STATE_READY;
  } else if (!s_ctx.rfid_authorized) {
    s_ctx.status = BIKE_STATE_WAIT_RFID;
  } else {
    s_ctx.status = BIKE_STATE_WAIT_HELMET;
  }
}

esp_err_t bike_state_init(void) {
  bike_state_reset();
  ESP_LOGI(TAG, "Bike state initialized: %s",
           bike_state_status_to_string(s_ctx.status));
  return ESP_OK;
}

void bike_state_reset(void) {
  memset(&s_ctx, 0, sizeof(s_ctx));
  s_ctx.status = BIKE_STATE_LOCKED;
  bike_state_refresh_status();
}

void bike_state_set_rfid_authorized(bool authorized) {
  s_ctx.rfid_authorized = authorized;
  bike_state_refresh_status();
}

void bike_state_update_helmet(bool connected, bool helmet_ready,
                              bool alcohol_pass) {
  s_ctx.helmet_connected = connected;
  s_ctx.helmet_ready = helmet_ready;
  s_ctx.alcohol_pass = alcohol_pass;
  bike_state_refresh_status();
}

bool bike_state_is_hardcoded_uid_allowed(const uint8_t *uid, size_t uid_len) {
  (void)uid;
  (void)uid_len;

  /*
   * The user selected hardcoded UID authorization, but no real UID has been
   * provided yet. Keep fail-closed until the allowlist is filled in BIKE-004.
   */
  return false;
}

bool bike_state_is_ignition_allowed(void) {
  return s_ctx.rfid_authorized && s_ctx.helmet_connected &&
         s_ctx.helmet_ready && s_ctx.alcohol_pass;
}

bike_state_status_t bike_state_get_status(void) {
  return s_ctx.status;
}

const char *bike_state_status_to_string(bike_state_status_t status) {
  switch (status) {
    case BIKE_STATE_LOCKED:
      return "LOCKED";
    case BIKE_STATE_WAIT_RFID:
      return "WAIT_RFID";
    case BIKE_STATE_WAIT_HELMET:
      return "WAIT_HELMET";
    case BIKE_STATE_READY:
      return "READY";
    case BIKE_STATE_RIDE_ACTIVE:
      return "RIDE_ACTIVE";
    case BIKE_STATE_ERROR:
      return "ERROR";
    default:
      return "UNKNOWN";
  }
}
