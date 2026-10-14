#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
  BIKE_STATE_LOCKED = 0,
  BIKE_STATE_WAIT_RFID,
  BIKE_STATE_WAIT_HELMET,
  BIKE_STATE_READY,
  BIKE_STATE_RIDE_ACTIVE,
  BIKE_STATE_ERROR,
} bike_state_status_t;

esp_err_t bike_state_init(void);
void bike_state_reset(void);
void bike_state_set_rfid_authorized(bool authorized);
void bike_state_update_helmet(bool connected, bool helmet_ready,
                              bool alcohol_pass);
bool bike_state_is_hardcoded_uid_allowed(const uint8_t *uid, size_t uid_len);
bool bike_state_is_ignition_allowed(void);
bike_state_status_t bike_state_get_status(void);
const char *bike_state_status_to_string(bike_state_status_t status);
