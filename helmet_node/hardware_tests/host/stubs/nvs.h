#pragma once
#include <stddef.h>
#include "esp_err.h"
#define NVS_READWRITE 1
#define ESP_ERR_NVS_NOT_FOUND 3
typedef int nvs_handle_t;
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *out);
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t size);
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *value, size_t *size);
esp_err_t nvs_commit(nvs_handle_t handle);
void nvs_close(nvs_handle_t handle);
