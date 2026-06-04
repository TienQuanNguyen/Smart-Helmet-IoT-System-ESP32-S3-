#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GPS_DEFAULT_UART_PORT      1
#define GPS_DEFAULT_BAUD_RATE      9600
#define GPS_DEFAULT_RX_BUFFER_SIZE 1024
#define GPS_DEFAULT_MOCK_ENABLED   false

typedef struct {
    int uart_port;
    int rx_pin;
    int tx_pin;
    int baud_rate;
    int rx_buffer_size;
    bool mock_enabled;
} gps_config_t;

typedef struct {
    double latitude;
    double longitude;
    bool fix_valid;
    uint8_t satellites;
    float hdop;
} gps_data_t;

bool gps_init(void);
bool gps_init_with_config(const gps_config_t *config);
int gps_read_raw_line(char *buffer, size_t max_len, uint32_t timeout_ms);
bool gps_parse_nmea(const char *line, gps_data_t *out);
bool gps_read_data(gps_data_t *out);
