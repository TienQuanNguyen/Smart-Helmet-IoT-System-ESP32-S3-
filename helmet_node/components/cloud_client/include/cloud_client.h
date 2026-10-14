#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "system_state.h"

typedef struct {
    const char *source_event;
    uint32_t uptime_ms;
    system_state_t state;
} cloud_client_emergency_report_t;

bool cloud_client_init(void);
bool cloud_client_submit_emergency(
    const cloud_client_emergency_report_t *report);
bool cloud_client_is_enabled(void);
