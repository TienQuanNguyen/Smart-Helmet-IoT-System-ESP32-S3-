#pragma once

#include <stdbool.h>

#include "event_manager.h"

typedef enum {
    SYSTEM_STATE_BOOT = 0,
    SYSTEM_STATE_STARTUP_ALCOHOL_CHECK,
    SYSTEM_STATE_READY_TO_RIDE,
    SYSTEM_STATE_DRIVING_MONITORING,
    SYSTEM_STATE_ACCIDENT_DETECTED,
    SYSTEM_STATE_EMERGENCY_REPORTING,
    SYSTEM_STATE_LOW_POWER_IDLE
} system_state_t;

bool system_state_init(void);
system_state_t system_state_get(void);
void system_state_set(system_state_t new_state);
void system_state_handle_event(system_event_t event);
const char *system_state_to_string(system_state_t state);
const char *system_event_to_string(system_event_t event);
