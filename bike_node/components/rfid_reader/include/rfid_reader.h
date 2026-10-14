#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define RFID_READER_MAX_UID_LEN 10U

typedef struct {
  uint8_t bytes[RFID_READER_MAX_UID_LEN];
  size_t length;
} rfid_reader_uid_t;

typedef struct {
  int cs_gpio;
  int sck_gpio;
  int miso_gpio;
  int mosi_gpio;
  int rst_gpio;
  uint32_t clock_speed_hz;
} rfid_reader_config_t;

esp_err_t rfid_reader_init(const rfid_reader_config_t *config);
bool rfid_reader_is_initialized(void);
esp_err_t rfid_reader_poll_uid(rfid_reader_uid_t *uid, bool *card_present);
