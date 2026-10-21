#include "hardware_test_internal.h"

#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mq3_driver.h"

#ifndef PHASE4_MQ3_SESSION_ID
#define PHASE4_MQ3_SESSION_ID "B03"
#endif

#ifndef PHASE4_MQ3_CONDITION
#define PHASE4_MQ3_CONDITION "WARMUP"
#endif

/* Set to 0 to use the fixed PHASE4_MQ3_CONDITION label instead. */
#ifndef PHASE4_MQ3_AUTO_CONDITION
#define PHASE4_MQ3_AUTO_CONDITION 1
#endif
#define PHASE4_MQ3_WARMUP_DURATION_MS (15ULL * 60ULL * 1000ULL)
#define PHASE4_MQ3_CLEAN_DURATION_MS (5ULL * 60ULL * 1000ULL)

#define PHASE4_MQ3_WINDOW_SAMPLES 16U
#define PHASE4_MQ3_SAMPLE_INTERVAL_US 5000LL
#define PHASE4_MQ3_WINDOW_PERIOD_US 200000LL
#define PHASE4_MQ3_COARSE_WAIT_MARGIN_US 15000LL

static const char *TAG = "test_phase4";

/* Labels describe the scheduled experiment, not detected alcohol levels.
 * Time is measured from sensor power-on at the start of each sample window.
 * ALCOHOL continues until the session is stopped/reset.
 */
static const char *phase4_condition_at(uint64_t timestamp_ms) {
  if (!PHASE4_MQ3_AUTO_CONDITION) {
    return PHASE4_MQ3_CONDITION;
  }
  if (timestamp_ms < PHASE4_MQ3_WARMUP_DURATION_MS) {
    return "WARMUP";
  }
  if (timestamp_ms <
      PHASE4_MQ3_WARMUP_DURATION_MS + PHASE4_MQ3_CLEAN_DURATION_MS) {
    return "CLEAN";
  }
  return "ALCOHOL";
}

typedef struct {
  uint32_t raw_avg;
  uint32_t voltage_mv;
  uint16_t raw_min;
  uint16_t raw_max;
  double raw_stddev;
} phase4_window_stats_t;

static bool phase4_condition_is_valid(const char *condition) {
  static const char *const valid_conditions[] = {
      "WARMUP", "CLEAN", "NOISE", "ALCOHOL", "RECOVERY",
  };

  for (size_t i = 0; i < sizeof(valid_conditions) / sizeof(valid_conditions[0]);
       ++i) {
    if (strcmp(condition, valid_conditions[i]) == 0) {
      return true;
    }
  }
  return false;
}

static bool phase4_session_id_is_valid(const char *session_id) {
  return (session_id != NULL) && (session_id[0] != '\0') &&
         (strpbrk(session_id, ",\r\n") == NULL);
}

static void phase4_wait_until_us(int64_t deadline_us) {
  while (true) {
    const int64_t remaining_us = deadline_us - esp_timer_get_time();
    if (remaining_us <= 0) {
      return;
    }

    if (remaining_us > PHASE4_MQ3_COARSE_WAIT_MARGIN_US) {
      const uint32_t delay_ms = (uint32_t)((remaining_us - 5000LL) / 1000LL);
      const TickType_t delay_ticks = pdMS_TO_TICKS(delay_ms);
      if (delay_ticks > 0) {
        vTaskDelay(delay_ticks);
        continue;
      }
    }

    esp_rom_delay_us((uint32_t)remaining_us);
  }
}

static bool phase4_collect_window(int64_t window_start_us,
                                  phase4_window_stats_t *stats) {
  if (stats == NULL) {
    return false;
  }

  uint32_t raw_sum = 0;
  uint64_t raw_squared_sum = 0;
  double voltage_mv_sum = 0.0;
  uint16_t raw_min = UINT16_MAX;
  uint16_t raw_max = 0;

  for (uint32_t i = 0; i < PHASE4_MQ3_WINDOW_SAMPLES; ++i) {
    phase4_wait_until_us(window_start_us +
                         (int64_t)i * PHASE4_MQ3_SAMPLE_INTERVAL_US);

    uint16_t raw = 0;
    float calibrated_voltage = 0.0f;
    if (!mq3_read_raw(&raw) ||
        !mq3_convert_raw_to_voltage(raw, &calibrated_voltage)) {
      return false;
    }

    raw_sum += raw;
    raw_squared_sum += (uint64_t)raw * (uint64_t)raw;
    voltage_mv_sum += (double)calibrated_voltage * 1000.0;
    if (raw < raw_min) {
      raw_min = raw;
    }
    if (raw > raw_max) {
      raw_max = raw;
    }
  }

  const double raw_mean = (double)raw_sum / (double)PHASE4_MQ3_WINDOW_SAMPLES;
  double raw_variance =
      (double)raw_squared_sum / (double)PHASE4_MQ3_WINDOW_SAMPLES -
      raw_mean * raw_mean;
  if (raw_variance < 0.0) {
    raw_variance = 0.0;
  }

  stats->raw_avg =
      (raw_sum + PHASE4_MQ3_WINDOW_SAMPLES / 2U) / PHASE4_MQ3_WINDOW_SAMPLES;
  stats->voltage_mv =
      (uint32_t)(voltage_mv_sum / (double)PHASE4_MQ3_WINDOW_SAMPLES + 0.5);
  stats->raw_min = raw_min;
  stats->raw_max = raw_max;
  stats->raw_stddev = sqrt(raw_variance);
  return true;
}

static void phase4_read_task(void *arg) {
  (void)arg;
  mq3_power_on();

  const int64_t power_on_us = esp_timer_get_time();
  int64_t next_window_us = power_on_us;

  printf("timestamp_ms,session_id,condition,raw_avg,voltage_mv,raw_min,"
         "raw_max,raw_stddev\n");
  fflush(stdout);
  while (true) {
    phase4_wait_until_us(next_window_us);
    const int64_t window_start_us = esp_timer_get_time();
    phase4_window_stats_t stats = {0};
    if (!phase4_collect_window(window_start_us, &stats)) {
      ESP_LOGE(TAG, "Dataset collection stopped: MQ-3 ADC read failed");
      mq3_power_off();
      vTaskDelete(NULL);
      return;
    }

    const uint64_t timestamp_ms =
        (uint64_t)(window_start_us - power_on_us) / 1000ULL;
    printf("%" PRIu64 ",%s,%s,%" PRIu32 ",%" PRIu32 ",%u,%u,%.1f\n",
           timestamp_ms, PHASE4_MQ3_SESSION_ID,
           phase4_condition_at(timestamp_ms), stats.raw_avg, stats.voltage_mv,
           (unsigned)stats.raw_min, (unsigned)stats.raw_max, stats.raw_stddev);
    fflush(stdout);

    next_window_us += PHASE4_MQ3_WINDOW_PERIOD_US;
    if (next_window_us <= window_start_us) {
      next_window_us = window_start_us + PHASE4_MQ3_WINDOW_PERIOD_US;
    }
  }
}

bool phase4_mq3_test_start(const hardware_test_config_t *config) {
  if (config == NULL) {
    ESP_LOGE(TAG, "FAIL: MQ-3 test config is NULL");
    return false;
  }

  if (!phase4_session_id_is_valid(PHASE4_MQ3_SESSION_ID) ||
      !phase4_condition_is_valid(PHASE4_MQ3_CONDITION)) {
    ESP_LOGE(TAG, "FAIL: invalid CSV session/condition; allowed conditions are "
                  "WARMUP, CLEAN, NOISE, ALCOHOL, RECOVERY");
    return false;
  }

  const mq3_config_t mq3_config = {
      .adc_pin = config->mq3_adc_pin,
      .adc_unit = MQ3_DEFAULT_ADC_UNIT,
      .adc_channel = MQ3_DEFAULT_ADC_CHANNEL,
      .adc_atten = MQ3_DEFAULT_ATTEN,
      .power_en_pin = config->mq3_power_en_pin,
      .alcohol_threshold_voltage = config->mq3_alcohol_threshold_voltage,
      .mock_enabled = false,
  };

  if (!mq3_init_with_config(&mq3_config)) {
    ESP_LOGE(TAG, "FAIL: MQ-3 initialization failed");
    return false;
  }

  float zero_voltage = 0.0f;
  float mid_voltage = 0.0f;
  float high_voltage = 0.0f;
  if (!mq3_convert_raw_to_voltage(0U, &zero_voltage) ||
      !mq3_convert_raw_to_voltage(2048U, &mid_voltage) ||
      !mq3_convert_raw_to_voltage(4095U, &high_voltage) ||
      (zero_voltage > mid_voltage) || (mid_voltage >= high_voltage)) {
    ESP_LOGE(TAG, "FAIL: calibrated conversion zero=%.3fV mid=%.3fV high=%.3fV",
             zero_voltage, mid_voltage, high_voltage);
    return false;
  }
  ESP_LOGI(TAG,
           "PREFLIGHT PASS: calibrated conversion zero=%.3fV mid=%.3fV "
           "high=%.3fV",
           zero_voltage, mid_voltage, high_voltage);

  ESP_LOGW(TAG,
           "Logged voltage is at GPIO%d after the divider; verify it never "
           "exceeds 3.3V",
           config->mq3_adc_pin);
  return xTaskCreate(phase4_read_task, "phase4_test", 4096, NULL, 4, NULL) ==
         pdPASS;
}
