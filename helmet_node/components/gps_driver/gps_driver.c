#include "gps_driver.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pin_config.h"
#include "system_config.h"

#define GPS_DEFAULT_RX_PIN 16
#define GPS_DEFAULT_TX_PIN 17
#define GPS_UART_TIMEOUT_STEP_MS 20
#define GPS_READ_DATA_MAX_LINES 5
#define GPS_MAX_NMEA_FIELDS 20

static const char *TAG = "gps_driver";

static const char *GPS_MOCK_GGA_LINE =
    "$GPGGA,021530.00,1049.3860,N,10637.7820,E,1,08,1.2,12.3,M,0.0,M,,*6D";

static int s_uart_port = GPS_DEFAULT_UART_PORT;
static bool s_initialized;
static bool s_mock_enabled;
static TaskHandle_t s_reader_task;
static portMUX_TYPE s_cache_lock = portMUX_INITIALIZER_UNLOCKED;
static gps_data_t s_cache;
static int64_t s_cache_time_us;
static bool s_cache_present;

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

  if (!gps_check_err(
          uart_param_config((uart_port_t)config->uart_port, &uart_config),
          "uart_param_config")) {
    return false;
  }

  if (!gps_check_err(
          uart_set_pin((uart_port_t)config->uart_port, config->tx_pin,
                       config->rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE),
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

static int gps_hex_value(char value) {
  if ((value >= '0') && (value <= '9')) {
    return value - '0';
  }
  if ((value >= 'A') && (value <= 'F')) {
    return value - 'A' + 10;
  }
  if ((value >= 'a') && (value <= 'f')) {
    return value - 'a' + 10;
  }
  return -1;
}

static bool gps_is_line_terminator(char value) {
  return (value == '\0') || (value == '\r') || (value == '\n');
}

static bool gps_validate_nmea_checksum(const char *line) {
  const char *checksum = strchr(line, '*');
  if (checksum == NULL) {
    return false;
  }

  if ((checksum[1] == '\0') || (checksum[2] == '\0')) {
    ESP_LOGW(TAG, "Malformed NMEA checksum");
    return false;
  }

  const int high_nibble = gps_hex_value(checksum[1]);
  const int low_nibble = gps_hex_value(checksum[2]);
  if ((high_nibble < 0) || (low_nibble < 0) ||
      !gps_is_line_terminator(checksum[3])) {
    ESP_LOGW(TAG, "Malformed NMEA checksum");
    return false;
  }

  uint8_t calculated = 0;
  for (const char *cursor = line + 1; cursor < checksum; ++cursor) {
    calculated ^= (uint8_t)(*cursor);
  }

  const uint8_t expected = (uint8_t)((high_nibble << 4) | low_nibble);
  if (calculated != expected) {
    ESP_LOGW(TAG, "Invalid NMEA checksum: expected=0x%02X actual=0x%02X",
             expected, calculated);
    return false;
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
  if ((end == value) || *end || !isfinite(raw) || raw < 0.0 || raw > 18000.0) {
    return NAN;
  }

  int degrees = (int)(raw / 100.0);
  double minutes = raw - ((double)degrees * 100.0);
  if (minutes >= 60.0) return NAN;
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

  char *end = NULL;
  long fix_quality = strtol(fields[6], &end, 10);
  if (end == fields[6] || *end || fix_quality < 0 || fix_quality > 8)
    return false;
  out->fix_valid = fix_quality > 0;
  out->latitude = gps_nmea_coord_to_decimal(fields[2], fields[3]);
  out->longitude = gps_nmea_coord_to_decimal(fields[4], fields[5]);
  long satellites = strtol(fields[7], &end, 10);
  if (end == fields[7] || *end || satellites < 0 || satellites > UINT8_MAX)
    return false;
  out->satellites = (uint8_t)satellites;
  out->hdop = (float)strtod(fields[8], &end);
  if (*end || !isfinite(out->hdop) || out->hdop < 0 ||
      (out->fix_valid && end == fields[8]))
    return false;
  if (out->fix_valid &&
      (!isfinite(out->latitude) || !isfinite(out->longitude) ||
       fabs(out->latitude) > 90 || fabs(out->longitude) > 180 ||
       !fields[2][0] || !fields[4][0] ||
       (strcmp(fields[3], "N") && strcmp(fields[3], "S")) ||
       (strcmp(fields[5], "E") && strcmp(fields[5], "W")) ||
       !isfinite(out->hdop) || out->hdop < 0))
    return false;

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
  if (s_reader_task && xTaskGetCurrentTaskHandle() != s_reader_task) {
    return -1;
  }
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
    int read_len = uart_read_bytes((uart_port_t)s_uart_port, &byte, 1,
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

  if (((strncmp(line, "$GPGGA", 6) != 0) &&
       (strncmp(line, "$GNGGA", 6) != 0)) ||
      line[6] != ',') {
    return false;
  }

  if (!gps_validate_nmea_checksum(line)) {
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
  char *line_end = strpbrk(line_copy, "\r\n");
  if (line_end != NULL) {
    *line_end = '\0';
  }

  memset(out, 0, sizeof(*out));
  return gps_parse_gga_fields(line_copy, out);
}

bool gps_read_data(gps_data_t *out) {
  if (s_reader_task && xTaskGetCurrentTaskHandle() != s_reader_task) {
    gps_snapshot_t snapshot;
    if (out == NULL || !gps_get_snapshot(&snapshot)) return false;
    *out = snapshot.data;
    return true;
  }
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
  for (uint8_t attempt = 0; attempt < GPS_READ_DATA_MAX_LINES; ++attempt) {
    int len = gps_read_raw_line(line, sizeof(line), GPS_READ_TIMEOUT_MS);
    if (len <= 0) {
      return false;
    }

    if (gps_parse_nmea(line, out)) {
      return true;
    }
  }

  return false;
}

static void gps_service_task(void *arg) {
  (void)arg;
  for (;;) {
    gps_data_t data;
    if (gps_read_data(&data)) {
      portENTER_CRITICAL(&s_cache_lock);
      s_cache = data;
      s_cache_time_us = esp_timer_get_time();
      s_cache_present = true;
      portEXIT_CRITICAL(&s_cache_lock);
    }
    vTaskDelay(pdMS_TO_TICKS(s_mock_enabled ? GPS_SAMPLE_PERIOD_MS : 20));
  }
}

bool gps_start_service(void) {
  if (s_reader_task) return true;
  if (!s_initialized) return false;
  return xTaskCreate(gps_service_task, "gps_reader", 3072, NULL, 4,
                     &s_reader_task) == pdPASS;
}

bool gps_get_snapshot(gps_snapshot_t *out) {
  if (!out) return false;
  portENTER_CRITICAL(&s_cache_lock);
  out->data = s_cache;
  bool present = s_cache_present;
  int64_t age = (esp_timer_get_time() - s_cache_time_us) / 1000;
  portEXIT_CRITICAL(&s_cache_lock);
  out->age_ms = !present || age > UINT32_MAX ? UINT32_MAX : (uint32_t)age;
  out->data.fix_valid = present && out->data.fix_valid && out->age_ms <= 5000;
  return present;
}
