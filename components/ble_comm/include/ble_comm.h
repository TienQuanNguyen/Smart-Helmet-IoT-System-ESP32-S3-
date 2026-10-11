#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ble_protocol.h"
#include "system_state.h"

bool ble_comm_init(void);
bool ble_comm_is_enabled(void);
bool ble_comm_notify_state(system_state_t state, uint32_t uptime_ms);
bool ble_comm_send_emergency(const char *source_event, uint32_t uptime_ms);
ble_helmet_status_packet_t ble_comm_make_status_packet(system_state_t state,
                                                       uint32_t uptime_ms);
