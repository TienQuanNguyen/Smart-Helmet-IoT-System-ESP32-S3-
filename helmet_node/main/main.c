#include <stdatomic.h>
#include <string.h>

#include "accident_detector.h"
#include "app_test.h"
#include "ble_comm.h"
#include "cloud_client.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "event_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gps_driver.h"
#include "hardware_test.h"
#include "helmet_io.h"
#include "mpu6050_driver.h"
#include "mq3_driver.h"
#include "pin_config.h"
#include "sdkconfig.h"
#include "system_config.h"
#include "system_state.h"
#include "wifi_manager.h"

#define SYSTEM_EVENT_WAIT_TIMEOUT_MS 20

static const char *TAG = SYSTEM_NAME;
static atomic_bool s_startup_fault;
static atomic_bool s_imu_valid, s_alcohol_valid, s_alcohol_detected;
static _Atomic float s_accel_mag_g, s_alcohol_voltage;
static atomic_uint s_alcohol_raw;

static uint32_t uptime_ms(void) {
  return (uint32_t)(esp_timer_get_time() / 1000);
}

static void task_alcohol_check(void *arg) {
  (void)arg;
  s_alcohol_valid = false;
  mq3_power_on();
  ESP_LOGI(TAG, "MQ-3 warm-up: %u ms (FSM remains responsive)",
           MQ3_WARMUP_TIME_MS);
  vTaskDelay(pdMS_TO_TICKS(MQ3_WARMUP_TIME_MS));
  uint32_t raw_sum = 0;
  float voltage_sum = 0;
  bool ok = true;
  for (unsigned i = 0; i < MQ3_SAMPLE_COUNT; ++i) {
    uint16_t raw;
    float voltage;
    if (!mq3_read_raw(&raw) || !mq3_convert_raw_to_voltage(raw, &voltage)) {
      ok = false;
      break;
    }
    raw_sum += raw;
    voltage_sum += voltage;
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  mq3_power_off();
  float average = voltage_sum / MQ3_SAMPLE_COUNT;
  bool pass = ok && !mq3_is_alcohol_detected(average);
  s_alcohol_raw = raw_sum / MQ3_SAMPLE_COUNT;
  s_alcohol_voltage = average;
  s_alcohol_detected = ok && !pass;
  s_alcohol_valid = ok;
  ESP_LOGI(TAG, "MQ-3 raw_avg=%lu voltage_avg=%.3fV result=%s sensor_ok=%d",
           (unsigned long)(raw_sum / MQ3_SAMPLE_COUNT), average,
           pass ? "PASS" : "FAIL", ok);
  system_event_t event =
      pass ? SYSTEM_EVENT_ALCOHOL_PASS : SYSTEM_EVENT_ALCOHOL_FAIL;
  while (!event_manager_publish(event))
    vTaskDelay(pdMS_TO_TICKS(100));
  vTaskDelete(NULL);
}

static void update_io(system_state_t state, system_state_t previous,
                      system_event_t event, uint32_t now) {
  helmet_io_pattern_t pattern = HELMET_IO_ERROR;
  switch (state) {
  case SYSTEM_STATE_BOOT:
    pattern = HELMET_IO_BOOT;
    break;
  case SYSTEM_STATE_STARTUP_ALCOHOL_CHECK:
    pattern = HELMET_IO_ALCOHOL_CHECK;
    break;
  case SYSTEM_STATE_READY_TO_RIDE:
    pattern = HELMET_IO_READY;
    break;
  case SYSTEM_STATE_DRIVING_MONITORING:
    pattern = HELMET_IO_DRIVING;
    break;
  case SYSTEM_STATE_ACCIDENT_DETECTED:
    pattern = HELMET_IO_ACCIDENT;
    break;
  case SYSTEM_STATE_EMERGENCY_REPORTING:
    pattern = HELMET_IO_EMERGENCY;
    break;
  default:
    break;
  }
  helmet_io_set_pattern(pattern);
  if (s_startup_fault && state != SYSTEM_STATE_EMERGENCY_REPORTING)
    helmet_io_set_pattern(HELMET_IO_ERROR);
  if (state == SYSTEM_STATE_ACCIDENT_DETECTED ||
      state == SYSTEM_STATE_EMERGENCY_REPORTING) {
    if (state != previous || event == SYSTEM_EVENT_SOS_PRESSED)
      helmet_io_alert(true, now);
  } else if (event == SYSTEM_EVENT_ALCOHOL_FAIL) {
    helmet_io_alert(false, now);
  }
  helmet_io_tick(now);
}

static bool init_mpu6050_driver(void) {
  const mpu6050_config_t config = {
      .i2c_port = MPU6050_DEFAULT_I2C_PORT,
      .sda_pin = PIN_I2C_SDA,
      .scl_pin = PIN_I2C_SCL,
      .clock_speed_hz = MPU6050_DEFAULT_I2C_CLOCK_HZ,
      .device_address = MPU6050_DEFAULT_ADDRESS,
      .accel_range = MPU6050_DEFAULT_ACCEL_RANGE,
      .gyro_range = MPU6050_DEFAULT_GYRO_RANGE,
      .dlpf = MPU6050_DEFAULT_DLPF,
      .sample_rate_divider = MPU6050_DEFAULT_SAMPLE_DIVIDER,
      .mock_enabled = USE_MOCK_SENSOR_DATA != 0,
  };

  if (!mpu6050_init_with_config(&config)) {
    ESP_LOGE(TAG, "MPU6050 init failed");
    return false;
  }

  uint8_t who_am_i = 0;
  if (mpu6050_read_who_am_i(&who_am_i)) {
    ESP_LOGI(TAG, "MPU6050 WHO_AM_I=0x%02X", who_am_i);
  }

  return true;
}

static bool read_accel_sample(uint32_t timestamp_ms, mpu6050_accel_t *accel) {
  if (accel == NULL) {
    return false;
  }

  if (USE_MOCK_SENSOR_DATA != 0) {
    return app_test_get_mock_imu_sample(&accel->x, &accel->y, &accel->z,
                                        timestamp_ms);
  }

  return mpu6050_read_accel_g(accel);
}

static void task_imu_monitor(void *arg) {
  (void)arg;

  uint32_t timestamp_ms = 0;
  bool impact_event_sent = false;
  bool fall_event_sent = false;

  while (true) {
    mpu6050_accel_t accel = {0};
    accident_result_t result = {0};

    if (read_accel_sample(timestamp_ms, &accel) &&
        accident_detector_update(accel.x, accel.y, accel.z, timestamp_ms,
                                 &result)) {
      s_accel_mag_g = result.accel_magnitude_g;
      s_imu_valid = true;
      if (result.impact_detected && !impact_event_sent) {
        ESP_LOGW(TAG, "IMU impact: %.2fg tilt=%.1fdeg",
                 result.accel_magnitude_g, result.tilt_angle_deg);
        impact_event_sent = event_manager_publish(SYSTEM_EVENT_IMU_IMPACT);
      }

      if (result.fall_confirmed && !fall_event_sent) {
        ESP_LOGW(TAG, "Fall confirmed: %.2fg tilt=%.1fdeg",
                 result.accel_magnitude_g, result.tilt_angle_deg);
        fall_event_sent = event_manager_publish(SYSTEM_EVENT_FALL_CONFIRMED);
      }
    } else {
      s_imu_valid = false;
      if (timestamp_ms % 5000 < IMU_SAMPLE_PERIOD_MS)
        ESP_LOGW(TAG, "Failed to update accident detector");
    }

    vTaskDelay(pdMS_TO_TICKS(IMU_SAMPLE_PERIOD_MS));
    timestamp_ms = uptime_ms();
  }
}

static void task_system_manager(void *arg) {
  (void)arg;

  uint32_t elapsed_ms = 0;
  uint32_t last_status_ms = 0, last_health_ms = 0;
  bool report_pending = false;
  cloud_client_emergency_report_t pending_report = {0};
  uint32_t last_report_try = 0;
  const char *emergency_source = "UNKNOWN";

  while (true) {
    elapsed_ms = uptime_ms();
    app_test_publish_mock_events_if_needed(elapsed_ms);

    system_event_t event = SYSTEM_EVENT_NONE;
    bool event_received =
        event_manager_wait(&event, SYSTEM_EVENT_WAIT_TIMEOUT_MS);
    if (event_received) {
      ESP_LOGI(TAG, "System event: %s", system_event_to_string(event));
    }

    system_state_t previous_state = system_state_get();
    if (event == SYSTEM_EVENT_SOS_PRESSED || event == SYSTEM_EVENT_IMU_IMPACT ||
        event == SYSTEM_EVENT_FALL_CONFIRMED)
      emergency_source = system_event_to_string(event);
    system_state_handle_event(event);
    system_state_t current_state = system_state_get();
    update_io(current_state, previous_state, event, elapsed_ms);
    if (current_state != previous_state ||
        elapsed_ms - last_status_ms >= 1000) {
      if (!ble_comm_notify_state(current_state, elapsed_ms))
        ESP_LOGD(TAG, "BLE notify unavailable");
      cloud_client_telemetry_t telemetry = {
          .state = current_state,
          .uptime_ms = elapsed_ms,
          .imu_valid = s_imu_valid,
          .accel_mag_g = s_accel_mag_g,
          .alcohol_valid = s_alcohol_valid,
          .alcohol_raw = s_alcohol_raw,
          .alcohol_voltage = s_alcohol_voltage,
          .alcohol_detected = s_alcohol_detected,
          .fall_detected = current_state == SYSTEM_STATE_ACCIDENT_DETECTED ||
                           (current_state == SYSTEM_STATE_EMERGENCY_REPORTING &&
                            (strcmp(emergency_source, "IMU_IMPACT") == 0 ||
                             strcmp(emergency_source, "FALL_CONFIRMED") == 0)),
          .sos_latched = false};
      /* Source is retained across event-loop ticks for an active emergency. */
      telemetry.sos_latched =
          current_state == SYSTEM_STATE_EMERGENCY_REPORTING &&
          strcmp(emergency_source, "SOS_PRESSED") == 0;
      cloud_client_update_telemetry(&telemetry);
      last_status_ms = elapsed_ms;
    }

    if ((current_state == SYSTEM_STATE_EMERGENCY_REPORTING) &&
        (previous_state != SYSTEM_STATE_EMERGENCY_REPORTING ||
         event == SYSTEM_EVENT_SOS_PRESSED)) {
      const char *source_event = emergency_source;
      const cloud_client_emergency_report_t report = {
          .source_event = source_event,
          .uptime_ms = elapsed_ms,
          .state = current_state,
      };

      if (!ble_comm_send_emergency(source_event, elapsed_ms))
        ESP_LOGW(TAG, "BLE emergency pending");
      pending_report = report;
#if CONFIG_SMART_HELMET_CLOUD_ENABLE
      report_pending = true;
#else
      report_pending = false;
#endif
      if (!report_pending)
        ESP_LOGW(TAG, "Cloud disabled; emergency available locally only");
    }

    if (event_received) {
      ESP_LOGI(TAG, "System state: %s", system_state_to_string(current_state));
    }

    if (report_pending && elapsed_ms - last_report_try >= 1000) {
      report_pending = !cloud_client_submit_emergency(&pending_report);
      last_report_try = elapsed_ms;
      if (report_pending)
        ESP_LOGW(TAG, "Emergency awaiting durable outbox acceptance");
    }
    if (elapsed_ms - last_health_ms >= 60000) {
      ESP_LOGI(TAG, "health uptime=%lu heap=%lu min_heap=%lu stack=%u",
               (unsigned long)elapsed_ms,
               (unsigned long)esp_get_free_heap_size(),
               (unsigned long)esp_get_minimum_free_heap_size(),
               (unsigned)uxTaskGetStackHighWaterMark(NULL));
      last_health_ms = elapsed_ms;
    }
  }
}

void app_main(void) {
  ESP_LOGI(TAG, "Booting %s", SYSTEM_NAME);
  ESP_LOGI(TAG, "Target board: ESP32-S3 DevKit");

  if (HARDWARE_TEST_MODE != 0) {
    const hardware_test_config_t test_config = {
        .phase = HARDWARE_TEST_PHASE,
        .i2c_sda_pin = PIN_I2C_SDA,
        .i2c_scl_pin = PIN_I2C_SCL,
        .mpu_int_pin = PIN_MPU6050_INT,
        .gps_rx_pin = PIN_GPS_RX,
        .gps_tx_pin = PIN_GPS_TX,
        .mq3_adc_pin = PIN_MQ3_ADC,
        .mq3_power_en_pin = PIN_MQ3_POWER_EN,
        .mq3_warmup_time_ms = MQ3_WARMUP_TIME_MS,
        .mq3_sample_count = MQ3_SAMPLE_COUNT,
        .mq3_alcohol_threshold_voltage = MQ3_ALCOHOL_THRESHOLD_VOLTAGE,
    };

    if (!hardware_test_start(&test_config)) {
      ESP_LOGE(TAG, "Hardware test phase %d failed to start",
               HARDWARE_TEST_PHASE);
    }
    return;
  }

  ESP_LOGI(TAG, "Sensor mock mode: %d", USE_MOCK_SENSOR_DATA);
  ESP_LOGI(TAG, "MPU6050 I2C pins: SDA=%d, SCL=%d", PIN_I2C_SDA, PIN_I2C_SCL);
  ESP_LOGI(TAG, "GPS UART pins: RX=%d, TX=%d", PIN_GPS_RX, PIN_GPS_TX);
  ESP_LOGI(TAG, "MQ-3 ADC GPIO=%d ADC%d channel=%d, power pin=%d", PIN_MQ3_ADC,
           MQ3_DEFAULT_ADC_UNIT + 1, MQ3_DEFAULT_ADC_CHANNEL, PIN_MQ3_POWER_EN);

  if (!event_manager_init() || !system_state_init()) {
    ESP_LOGE(TAG, "System init failed");
    return;
  }

  bool wifi_ok = wifi_manager_init(NULL);
  bool ble_ok = ble_comm_init();
  bool cloud_ok = cloud_client_init();
  if (!wifi_ok || !ble_ok || !cloud_ok)
    ESP_LOGE(TAG, "Communication startup failed; offline monitoring continues");
  if (!wifi_manager_start()) {
    ESP_LOGW(TAG,
             "Wi-Fi manager did not start; cloud reports are unavailable until "
             "phone hotspot config is valid");
  }

  const helmet_io_config_t io = {.sos_pin = PIN_SOS_BUTTON,
                                 .led_pin = PIN_LED_STATUS,
                                 .buzzer_pin = PIN_BUZZER,
                                 .sos_active = CONFIG_HELMET_SOS_ACTIVE_LEVEL,
                                 .led_active = CONFIG_HELMET_LED_ACTIVE_LEVEL,
                                 .buzzer_active =
                                     CONFIG_HELMET_BUZZER_ACTIVE_LEVEL,
                                 .buzzer_hz = CONFIG_HELMET_BUZZER_HZ};
  if (!helmet_io_init(&io)) {
    ESP_LOGE(TAG, "IO startup failed");
    return;
  }
  helmet_io_set_pattern(HELMET_IO_BOOT);
  helmet_io_tick(uptime_ms());
  bool gps_ok = gps_init();
  bool mq3_ok = mq3_init();
  bool imu_ok = accident_detector_init() && init_mpu6050_driver();
  s_startup_fault = !mq3_ok || !imu_ok;
  /* Run startup reads only before their owning tasks exist. */
  if (mq3_ok && imu_ok)
    app_test_run_startup_checks();
  if (gps_ok && !gps_start_service())
    ESP_LOGE(TAG, "GPS task startup failed");
  BaseType_t system_task_status =
      xTaskCreate(task_system_manager, "system_manager", 4096, NULL, 6, NULL);
  if (system_task_status != pdPASS) {
    ESP_LOGE(TAG, "Failed to create system tasks");
    return;
  }
  if (imu_ok && xTaskCreate(task_imu_monitor, "imu_monitor", 4096, NULL, 5,
                            NULL) != pdPASS) {
    ESP_LOGE(TAG, "IMU task creation failed");
    s_startup_fault = true;
  }
  if (!USE_MOCK_SENSOR_DATA && !s_startup_fault &&
      xTaskCreate(task_alcohol_check, "alcohol_check", 3072, NULL, 4, NULL) !=
          pdPASS) {
    ESP_LOGE(TAG, "MQ-3 task creation failed; authorization remains blocked");
    s_startup_fault = true;
  }
}
