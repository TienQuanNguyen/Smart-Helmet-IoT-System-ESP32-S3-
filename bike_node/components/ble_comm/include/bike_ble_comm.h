#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "helmet_bike_protocol.h"

esp_err_t bike_ble_comm_init(void);
bool bike_ble_comm_is_connected(void);
bool bike_ble_comm_is_runtime_enabled(void);
bool bike_ble_comm_get_status(ble_helmet_status_packet_t *status);
