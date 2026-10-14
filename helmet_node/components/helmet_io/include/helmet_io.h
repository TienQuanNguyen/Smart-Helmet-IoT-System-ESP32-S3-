#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
  int sos_pin, led_pin, buzzer_pin;
  /* -1 means unconfirmed: do not configure or drive the corresponding GPIO. */
  int sos_active, led_active, buzzer_active;
  int buzzer_hz; /* 0 active buzzer; >0 passive; -1 unconfirmed. */
} helmet_io_config_t;

typedef enum {
  HELMET_IO_BOOT,
  HELMET_IO_ALCOHOL_CHECK,
  HELMET_IO_READY,
  HELMET_IO_DRIVING,
  HELMET_IO_ACCIDENT,
  HELMET_IO_EMERGENCY,
  HELMET_IO_ERROR
} helmet_io_pattern_t;

bool helmet_io_init(const helmet_io_config_t *config);
void helmet_io_set_pattern(helmet_io_pattern_t pattern);
void helmet_io_alert(bool emergency, uint32_t now_ms);
void helmet_io_stop_buzzer(void);
void helmet_io_tick(uint32_t now_ms);
