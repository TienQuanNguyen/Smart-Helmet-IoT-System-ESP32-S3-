#include "hardware_test_internal.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gps_driver.h"

#define GPS_TEST_LINE_BUFFER_SIZE 128

static const char *TAG = "test_phase3";

typedef struct {
  const char *name;
  const char *sentence;
  bool expected_parse;
  bool expected_fix;
} phase3_parser_case_t;

static bool phase3_parser_preflight(void) {
  static const phase3_parser_case_t cases[] = {
      {
          .name = "valid GPGGA",
          .sentence = "$GPGGA,021530.00,1049.3860,N,10637.7820,E,1,08,1.2,"
                      "12.3,M,0.0,M,,*6D",
          .expected_parse = true,
          .expected_fix = true,
      },
      {
          .name = "valid GNGGA",
          .sentence = "$GNGGA,021530.00,1049.3860,N,10637.7820,E,1,08,1.2,"
                      "12.3,M,0.0,M,,*73",
          .expected_parse = true,
          .expected_fix = true,
      },
      {
          .name = "no-fix GPGGA",
          .sentence = "$GPGGA,021530.00,,,,,0,00,99.9,,,,,,*5A",
          .expected_parse = true,
          .expected_fix = false,
      },
      {
          .name = "non-GGA sentence",
          .sentence = "$GPRMC,021530.00,A,1049.3860,N,10637.7820,E,0.0,0.0,"
                      "010126,,,A*50",
          .expected_parse = false,
          .expected_fix = false,
      },
      {
          .name = "invalid checksum",
          .sentence = "$GPGGA,021530.00,1049.3860,N,10637.7820,E,1,08,1.2,"
                      "12.3,M,0.0,M,,*00",
          .expected_parse = false,
          .expected_fix = false,
      },
  };

  unsigned int pass_count = 0;
  unsigned int fail_count = 0;

  for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
    gps_data_t data = {0};
    const bool parsed = gps_parse_nmea(cases[index].sentence, &data);
    const bool passed =
        (parsed == cases[index].expected_parse) &&
        (!parsed || (data.fix_valid == cases[index].expected_fix));

    if (passed) {
      ++pass_count;
      ESP_LOGI(TAG, "PREFLIGHT PASS: %s", cases[index].name);
    } else {
      ++fail_count;
      ESP_LOGE(TAG,
               "PREFLIGHT FAIL: %s expected(parse=%d fix=%d) "
               "actual(parse=%d fix=%d)",
               cases[index].name, cases[index].expected_parse,
               cases[index].expected_fix, parsed, data.fix_valid);
    }
  }

  ESP_LOGI(TAG, "GPS PARSER PREFLIGHT: pass=%u fail=%u", pass_count,
           fail_count);
  return fail_count == 0;
}

static void phase3_read_task(void *arg) {
  (void)arg;
  char line[GPS_TEST_LINE_BUFFER_SIZE] = {0};

  while (true) {
    int length = gps_read_raw_line(line, sizeof(line), 1500);
    if (length > 0) {
      gps_data_t data = {0};
      ESP_LOGI(TAG, "NMEA: %s", line);
      if (gps_parse_nmea(line, &data)) {
        ESP_LOGI(TAG, "GGA parsed: fix=%d lat=%.6f lon=%.6f sats=%u hdop=%.1f",
                 data.fix_valid, data.latitude, data.longitude, data.satellites,
                 data.hdop);
        if (data.fix_valid) {
          ESP_LOGI(TAG, "HARDWARE PASS CANDIDATE: valid GPS fix received");
        }
      }
    } else if (length == 0) {
      ESP_LOGW(TAG, "No complete NMEA line; check wiring and 9600 baud");
    } else {
      ESP_LOGE(TAG, "FAIL: GPS read error");
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

bool phase3_gps_test_start(const hardware_test_config_t *config) {
  if ((config == NULL) || !phase3_parser_preflight()) {
    ESP_LOGE(TAG, "FAIL: GPS parser preflight failed");
    return false;
  }

  const gps_config_t gps_config = {
      .uart_port = GPS_DEFAULT_UART_PORT,
      .rx_pin = config->gps_rx_pin,
      .tx_pin = config->gps_tx_pin,
      .baud_rate = GPS_DEFAULT_BAUD_RATE,
      .rx_buffer_size = GPS_DEFAULT_RX_BUFFER_SIZE,
      .mock_enabled = false,
  };

  if (!gps_init_with_config(&gps_config)) {
    ESP_LOGE(TAG, "FAIL: GPS initialization failed");
    return false;
  }

  ESP_LOGI(TAG, "Waiting for NMEA on RX=%d TX=%d at 9600 baud",
           config->gps_rx_pin, config->gps_tx_pin);
  return xTaskCreate(phase3_read_task, "phase3_test", 4096, NULL, 4, NULL) ==
         pdPASS;
}
