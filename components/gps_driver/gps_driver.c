#include "gps_driver.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "pin_config.h"
#include "system_config.h"

#define GPS_DEFAULT_RX_PIN 16
#define GPS_DEFAULT_TX_PIN 17
#define GPS_UART_TIMEOUT_STEP_MS 20
#define GPS_MAX_NMEA_FIELDS 20

static const char *TAG = "gps_driver";

static const char *GPS_MOCK_GGA_LINE =
    "$GPGGA,021530.00,1049.3860,N,10637.7820,E,1,08,1.2,12.3,M,0.0,M,,*48";

static int s_uart_port = GPS_DEFAULT_UART_PORT;
static bool s_initialized;
static bool s_mock_enabled;

static bool gps_is_valid_config(const gps_config_t *config) {
  return (config != NULL) && (config->uart_port >= UART_NUM_0) &&
         (config->uart_port < UART_NUM_MAX) && (config->rx_pin >= 0) &&
         (config->tx_pin >= 0) && (config->baud_rate > 0) &&
         (config->rx_buffer_size >= 128);
}

static bool gps_check_err(esp_err_t err, const char *action) {
  if (err == ESP_OK) {
    return true;
  }

  ESP_LOGE(TAG, "%s failed: %s", action, esp_err_to_name(err));
  return false;
}

static bool gps_uart_init(const gps_config_t *config) {
  uart_config_t uart_config = {
      .baud_rate = config->baud_rate,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };

  if (!gps_check_err(uart_param_config((uart_port_t)config->uart_port,
                                       &uart_config),
                     "uart_param_config")) {
    return false;
  }

  if (!gps_check_err(uart_set_pin((uart_port_t)config->uart_port,
                                  config->tx_pin, config->rx_pin,
                                  UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE),
                     "uart_set_pin")) {
    return false;
  }

  esp_err_t err = uart_driver_install((uart_port_t)config->uart_port,
                                      config->rx_buffer_size, 0, 0, NULL, 0);
  if ((err != ESP_OK) && (err != ESP_ERR_INVALID_STATE)) {
    ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(err));
    return false;
  }

  if (err == ESP_ERR_INVALID_STATE) {
    ESP_LOGW(TAG, "UART driver already installed on port %d",
             config->uart_port);
  }

  return true;
}

static double gps_nmea_coord_to_decimal(const char *value, const char *hemi) {
  if ((value == NULL) || (hemi == NULL) || (value[0] == '\0') ||
      (hemi[0] == '\0')) {
    return 0.0;
  }

  char *end = NULL;
  double raw = strtod(value, &end);
  if ((end == value) || (raw <= 0.0)) {
    return 0.0;
  }

  int degrees = (int)(raw / 100.0);
  double minutes = raw - ((double)degrees * 100.0);
  double decimal = (double)degrees + (minutes / 60.0);

  if ((hemi[0] == 'S') || (hemi[0] == 'W')) {
    decimal = -decimal;
  }

  return decimal;
}

static bool gps_parse_gga_fields(char *line_copy, gps_data_t *out) {
  char *fields[GPS_MAX_NMEA_FIELDS] = {0};
  size_t field_count = 0;
  char *cursor = line_copy;

  while ((field_count < GPS_MAX_NMEA_FIELDS) && (cursor != NULL)) {
    fields[field_count++] = cursor;
    char *comma = strchr(cursor, ',');
    if (comma == NULL) {
      break;
    }
    *comma = '\0';
    cursor = comma + 1;
  }

  if (field_count < 10) {
    return false;
  }

  int fix_quality = atoi(fields[6]);
  out->fix_valid = fix_quality > 0;
  out->latitude = gps_nmea_coord_to_decimal(fields[2], fields[3]);
  out->longitude = gps_nmea_coord_to_decimal(fields[4], fields[5]);
  out->satellites = (uint8_t)atoi(fields[7]);
  out->hdop = (float)strtod(fields[8], NULL);

  return true;
}

bool gps_init(void) {
  const gps_config_t default_config = {
      .uart_port = GPS_DEFAULT_UART_PORT,
      .rx_pin = PIN_GPS_RX,
      .tx_pin = PIN_GPS_TX,
      .baud_rate = GPS_DEFAULT_BAUD_RATE,
      .rx_buffer_size = GPS_DEFAULT_RX_BUFFER_SIZE,
      .mock_enabled = USE_MOCK_SENSOR_DATA != 0,
  };

  return gps_init_with_config(&default_config);
}

bool gps_init_with_config(const gps_config_t *config) {
  if (s_initialized) {
    return true;
  }

  if (!gps_is_valid_config(config)) {
    ESP_LOGE(TAG, "Invalid GPS config");
    return false;
  }

  s_uart_port = config->uart_port;
  s_mock_enabled = config->mock_enabled;

  if (s_mock_enabled) {
    s_initialized = true;
    ESP_LOGW(TAG, "GPS mock mode enabled");
    return true;
  }

  if (!gps_uart_init(config)) {
    return false;
  }

  s_initialized = true;
  ESP_LOGI(TAG, "GPS initialized on UART%d, baud=%d", config->uart_port,
           config->baud_rate);

  return true;
}

int gps_read_raw_line(char *buffer, size_t max_len, uint32_t timeout_ms) {
  if ((buffer == NULL) || (max_len < 2)) {
    ESP_LOGE(TAG, "Invalid GPS line buffer");
    return -1;
  }

  if (!s_initialized) {
    ESP_LOGE(TAG, "GPS is not initialized");
    return -1;
  }

  if (s_mock_enabled) {
    int len = snprintf(buffer, max_len, "%s", GPS_MOCK_GGA_LINE);
    if ((len < 0) || ((size_t)len >= max_len)) {
      return -1;
    }
    return len;
  }

  size_t index = 0;
  uint32_t elapsed_ms = 0;
  uint8_t byte = 0;

  while ((index + 1) < max_len) {
    int read_len =
        uart_read_bytes((uart_port_t)s_uart_port, &byte, 1,
                        pdMS_TO_TICKS(GPS_UART_TIMEOUT_STEP_MS));
    if (read_len == 1) {
      if (byte == '\r') {
        continue;
      }
      if (byte == '\n') {
        if (index == 0) {
          continue;
        }
        break;
      }
      buffer[index++] = (char)byte;
      continue;
    }

    elapsed_ms += GPS_UART_TIMEOUT_STEP_MS;
    if (elapsed_ms >= timeout_ms) {
      break;
    }
  }

  buffer[index] = '\0';
  return (index > 0) ? (int)index : 0;
}

bool gps_parse_nmea(const char *line, gps_data_t *out) {
  if ((line == NULL) || (out == NULL)) {
    ESP_LOGE(TAG, "Invalid GPS parse input");
    return false;
  }

  if ((strncmp(line, "$GPGGA", 6) != 0) && (strncmp(line, "$GNGGA", 6) != 0)) {
    return false;
  }

  char line_copy[128] = {0};
  int len = snprintf(line_copy, sizeof(line_copy), "%s", line);
  if ((len < 0) || ((size_t)len >= sizeof(line_copy))) {
    ESP_LOGE(TAG, "NMEA line too long");
    return false;
  }

  char *checksum = strchr(line_copy, '*');
  if (checksum != NULL) {
    *checksum = '\0';
  }

  memset(out, 0, sizeof(*out));
  return gps_parse_gga_fields(line_copy, out);
}

bool gps_read_data(gps_data_t *out) {
  if (out == NULL) {
    ESP_LOGE(TAG, "GPS output is NULL");
    return false;
  }

  if (s_mock_enabled) {
    out->latitude = 10.8231;
    out->longitude = 106.6297;
    out->fix_valid = true;
    out->satellites = 8;
    out->hdop = 1.2f;
    return true;
  }

  char line[128] = {0};
  int len = gps_read_raw_line(line, sizeof(line), GPS_READ_TIMEOUT_MS);
  if (len <= 0) {
    return false;
  }

  return gps_parse_nmea(line, out);
}
