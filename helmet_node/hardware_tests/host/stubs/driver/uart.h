#pragma once
#include <stddef.h>
#include "esp_err.h"
#define UART_NUM_0 0
#define UART_NUM_MAX 3
#define UART_DATA_8_BITS 8
#define UART_PARITY_DISABLE 0
#define UART_STOP_BITS_1 1
#define UART_HW_FLOWCTRL_DISABLE 0
#define UART_SCLK_DEFAULT 0
#define UART_PIN_NO_CHANGE -1
typedef int uart_port_t;
typedef struct { int baud_rate, data_bits, parity, stop_bits, flow_ctrl, source_clk; } uart_config_t;
static inline esp_err_t uart_param_config(int port, const uart_config_t *cfg) {
  (void)port; (void)cfg; return ESP_OK;
}
static inline esp_err_t uart_set_pin(int port, int tx, int rx, int rts, int cts) {
  (void)port; (void)tx; (void)rx; (void)rts; (void)cts; return ESP_OK;
}
static inline esp_err_t uart_driver_install(int port, int rx, int tx, int size, void *queue, int flags) {
  (void)port; (void)rx; (void)tx; (void)size; (void)queue; (void)flags; return ESP_OK;
}
static inline int uart_read_bytes(int port, void *buf, unsigned count, unsigned timeout) {
  (void)port; (void)buf; (void)count; (void)timeout; return 0;
}
