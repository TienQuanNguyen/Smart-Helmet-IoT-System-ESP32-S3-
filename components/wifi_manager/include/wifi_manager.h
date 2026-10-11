#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool enabled;
    const char *ssid;
    const char *password;
    uint8_t max_retry_count;
} wifi_manager_config_t;

bool wifi_manager_init(const wifi_manager_config_t *config);
bool wifi_manager_start(void);
bool wifi_manager_stop(void);
bool wifi_manager_is_enabled(void);
bool wifi_manager_is_connected(void);
