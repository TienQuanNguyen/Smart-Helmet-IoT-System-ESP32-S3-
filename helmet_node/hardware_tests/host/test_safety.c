#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "bike_state.h"
#include "rfid_uid_match.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "gps_driver.h"
#include "helmet_bike_codec.h"
#include "helmet_io.h"
#include "system_state.h"

static int levels[16], directions[16], sos_events;
static bool queue_full;
static unsigned checks;
#define CHECK(expr) \
  do {              \
    ++checks;       \
    assert(expr);   \
  } while (0)
esp_err_t gpio_config(const gpio_config_t *c) {
  CHECK(c->pin_bit_mask == (1ULL << 6));
  CHECK(c->pull_up_en == 1 && c->pull_down_en == 0);
  return ESP_OK;
}
esp_err_t gpio_set_level(int pin, int level) {
  levels[pin] = level;
  return ESP_OK;
}
esp_err_t gpio_set_direction(int pin, int direction) {
  CHECK(levels[pin] == 0);
  directions[pin] = direction;
  return ESP_OK;
}
int gpio_get_level(int pin) { return levels[pin]; }
esp_err_t ledc_timer_config(const ledc_timer_config_t *c) {
  (void)c;
  return ESP_OK;
}
esp_err_t ledc_channel_config(const ledc_channel_config_t *c) {
  (void)c;
  return ESP_OK;
}
esp_err_t ledc_set_duty(int m, int c, int d) {
  (void)m;
  (void)c;
  (void)d;
  return ESP_OK;
}
esp_err_t ledc_update_duty(int m, int c) {
  (void)m;
  (void)c;
  return ESP_OK;
}
bool event_manager_publish(system_event_t event) {
  CHECK(event == SYSTEM_EVENT_SOS_PRESSED);
  if (queue_full) return false;
  ++sos_events;
  return true;
}

static void test_authorization(void) {
  CHECK(bike_state_init() == ESP_OK);
  CHECK(!bike_state_is_ignition_allowed());
  for (unsigned mask = 0; mask < 16; ++mask) {
    bike_state_set_rfid_authorized(mask & 1);
    bike_state_update_helmet(mask & 2, mask & 4, mask & 8);
    CHECK(bike_state_is_ignition_allowed() == (mask == 15));
  }
  bike_state_update_helmet(false, true, true);
  CHECK(!bike_state_is_ignition_allowed());
  uint8_t uid[] = {1, 2, 3, 4};
  CHECK(!bike_state_is_hardcoded_uid_allowed(uid, sizeof(uid)));
  CHECK(!rfid_uid_matches(uid, sizeof(uid), ""));
  CHECK(rfid_uid_matches(uid, sizeof(uid), "01:02:03:04"));
  CHECK(rfid_uid_matches(uid, sizeof(uid), "01020304"));
  CHECK(!rfid_uid_matches(uid, sizeof(uid), "01:02:03:05"));
  CHECK(!rfid_uid_matches(uid, sizeof(uid), "01:02:03"));
  CHECK(!rfid_uid_matches(uid, sizeof(uid), "01:02:03:04:05"));
  CHECK(!rfid_uid_matches(uid, sizeof(uid), "01-02-03-04"));
  CHECK(!rfid_uid_matches(uid, sizeof(uid), "01:02:03:0G"));
  CHECK(!rfid_uid_matches(uid, sizeof(uid), " 01:02:03:04"));
  CHECK(!rfid_uid_matches(NULL, 4, "01020304"));
  CHECK(!rfid_uid_matches(uid, 4, NULL));
  uint8_t long_uid[] = {0x04, 0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6, 7, 8, 9};
  CHECK(rfid_uid_matches(long_uid, 7, "04:a1:b2:c3:d4:e5:f6"));
  CHECK(rfid_uid_matches(long_uid, 10, "04:A1:B2:C3:D4:E5:F6:07:08:09"));
  CHECK(rfid_uid_matches(long_uid, 10, "04A1B2C3D4E5F6070809"));
  CHECK(!rfid_uid_matches(long_uid, 7, "04:A1:B2:C3"));
  CHECK(!rfid_uid_matches(long_uid, 5, "04A1B2C3D4"));
}
static void test_fsm(void) {
  CHECK(system_state_init());
  system_state_handle_event(SYSTEM_EVENT_NONE);
  CHECK(system_state_get() == SYSTEM_STATE_STARTUP_ALCOHOL_CHECK);
  system_state_handle_event(SYSTEM_EVENT_ALCOHOL_PASS);
  CHECK(system_state_get() == SYSTEM_STATE_READY_TO_RIDE);
  system_state_handle_event(SYSTEM_EVENT_NONE);
  CHECK(system_state_get() == SYSTEM_STATE_DRIVING_MONITORING);
  system_state_handle_event(SYSTEM_EVENT_IMU_IMPACT);
  CHECK(system_state_get() == SYSTEM_STATE_ACCIDENT_DETECTED);
  system_state_handle_event(SYSTEM_EVENT_NONE);
  CHECK(system_state_get() == SYSTEM_STATE_EMERGENCY_REPORTING);
  system_state_handle_event(SYSTEM_EVENT_ALCOHOL_FAIL);
  CHECK(system_state_get() == SYSTEM_STATE_EMERGENCY_REPORTING);
  system_state_handle_event(SYSTEM_EVENT_ALCOHOL_PASS);
  CHECK(system_state_get() == SYSTEM_STATE_EMERGENCY_REPORTING);
  for (int state = SYSTEM_STATE_BOOT; state <= SYSTEM_STATE_LOW_POWER_IDLE;
       ++state) {
    system_state_set((system_state_t)state);
    system_state_handle_event(SYSTEM_EVENT_SOS_PRESSED);
    CHECK(system_state_get() == SYSTEM_STATE_EMERGENCY_REPORTING);
  }
  system_state_init();
  system_state_handle_event(SYSTEM_EVENT_ALCOHOL_FAIL);
  CHECK(system_state_get() == SYSTEM_STATE_LOW_POWER_IDLE);
}
static void test_packets(void) {
  ble_helmet_status_packet_t p = {.version = 1,
                                  .state = 2,
                                  .flags = 1,
                                  .sequence = 255,
                                  .uptime_ms = 0x12345678},
                             decoded;
  uint8_t wire[8];
  helmet_status_encode(&p, wire);
  CHECK(wire[4] == 0x78 && wire[7] == 0x12);
  CHECK(helmet_status_decode(wire, 8, &decoded));
  CHECK(decoded.uptime_ms == p.uptime_ms && decoded.sequence == p.sequence);
  CHECK(!helmet_status_decode(wire, 7, &decoded));
  CHECK(!helmet_status_decode(wire, 9, &decoded));
  wire[0] = 2;
  CHECK(!helmet_status_decode(wire, 8, &decoded));
  wire[0] = 1;
  wire[1] = 255;
  CHECK(!helmet_status_decode(wire, 8, &decoded));
  wire[1] = 2;
  wire[2] = 128;
  CHECK(!helmet_status_decode(wire, 8, &decoded));
  decoded = p;
  CHECK(!helmet_status_is_newer(&decoded, &p));
  decoded.sequence = 0;
  ++decoded.uptime_ms;
  CHECK(helmet_status_is_newer(&decoded, &p));
  decoded.sequence = 254;
  CHECK(!helmet_status_is_newer(&decoded, &p));
  decoded.sequence = 0;
  decoded.uptime_ms = 0;
  CHECK(!helmet_status_is_newer(&decoded, &p));
  p.uptime_ms = UINT32_MAX;
  decoded.uptime_ms = 0;
  CHECK(helmet_status_is_newer(&decoded, &p));
}
static void test_io(void) {
  levels[6] = 1;
  helmet_io_config_t io = {.sos_pin = 6,
                           .led_pin = 2,
                           .buzzer_pin = 10,
                           .sos_active = 0,
                           .led_active = 1,
                           .buzzer_active = 1,
                           .buzzer_hz = 0};
  CHECK(helmet_io_init(&io));
  CHECK(levels[10] == 0 && directions[10] == GPIO_MODE_OUTPUT);
  helmet_io_tick(0);
  levels[6] = 0;
  helmet_io_tick(10);
  levels[6] = 1;
  helmet_io_tick(20);
  levels[6] = 0;
  helmet_io_tick(30);
  helmet_io_tick(69);
  CHECK(sos_events == 0);
  helmet_io_tick(70);
  CHECK(sos_events == 1);
  helmet_io_tick(1000);
  CHECK(sos_events == 1);
  levels[6] = 1;
  helmet_io_tick(1010);
  helmet_io_tick(1050);
  levels[6] = 0;
  helmet_io_tick(1060);
  queue_full = true;
  helmet_io_tick(1100);
  CHECK(sos_events == 1);
  queue_full = false;
  helmet_io_tick(1120);
  CHECK(sos_events == 2);
  helmet_io_tick(1140);
  CHECK(sos_events == 2);
  helmet_io_alert(false, 2000);
  helmet_io_tick(2000);
  CHECK(levels[10] == 1);
  helmet_io_tick(2300);
  CHECK(levels[10] == 0);
  helmet_io_alert(true, 3000);
  helmet_io_tick(3000);
  CHECK(levels[10] == 1);
  helmet_io_tick(3300);
  CHECK(levels[10] == 0);
  helmet_io_tick(3600);
  CHECK(levels[10] == 1);
  helmet_io_alert(false, 3650);
  helmet_io_tick(3900);
  CHECK(levels[10] == 0);
  helmet_io_tick(4200);
  CHECK(levels[10] == 1);
  helmet_io_stop_buzzer();
  helmet_io_tick(4220);
  CHECK(levels[10] == 0);
}
static void test_gps(void) {
  gps_data_t data;
  CHECK(gps_parse_nmea(
      "$GPGGA,021530.00,1049.3860,N,10637.7820,E,1,08,1.2,12.3,M,0.0,M,,*6D",
      &data));
  CHECK(data.fix_valid && data.satellites == 8);
  CHECK(gps_parse_nmea("$GPGGA,021530.00,,,,,0,00,99.9,,,,,,*5A", &data));
  CHECK(!data.fix_valid);
  CHECK(!gps_parse_nmea(
      "$GPGGA,021530.00,1049.3860,N,10637.7820,E,1,08,1.2,12.3,M,0.0,M,,*00",
      &data));
  CHECK(!gps_parse_nmea(
      "$GPGGA,021530.00,1049.3860,N,10637.7820,E,1,08,1.2,12.3,M,0.0,M,,",
      &data));
  const char *bad[] = {
      "GPGGA,021530.00,1069.0,N,10637.0,E,1,08,1.2,12.3,M,0.0,M,,",
      "GPGGA,021530.00,1049.0,Q,10637.0,E,1,08,1.2,12.3,M,0.0,M,,",
      "GPGGA,021530.00,nan,N,10637.0,E,1,08,1.2,12.3,M,0.0,M,,",
      "GPGGA,021530.00,1049.0,N,10637.0,E,1x,08,1.2,12.3,M,0.0,M,,",
      "GPGGA,021530.00,1049.0,N,10637.0,E,1,999,1.2,12.3,M,0.0,M,,"};
  for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
    unsigned checksum = 0;
    for (const char *p = bad[i]; *p; ++p) checksum ^= (unsigned char)*p;
    char line[128];
    snprintf(line, sizeof(line), "$%s*%02X", bad[i], checksum);
    CHECK(!gps_parse_nmea(line, &data));
  }
  gps_snapshot_t snapshot;
  CHECK(!gps_get_snapshot(&snapshot));
  CHECK(!snapshot.data.fix_valid && snapshot.age_ms == UINT32_MAX);
}
int main(void) {
  test_authorization();
  test_fsm();
  test_packets();
  test_io();
  test_gps();
  printf("PASS: %u safety checks (host mocks, not hardware)\n", checks);
  return 0;
}
