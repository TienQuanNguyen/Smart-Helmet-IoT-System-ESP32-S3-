#include "helmet_io.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "event_manager.h"

static helmet_io_config_t s_config;
static helmet_io_pattern_t s_pattern;
static bool s_ready, s_candidate, s_pressed, s_pending, s_alert, s_emergency;
static uint32_t s_changed_ms, s_alert_ms;
static int s_led_level = -1, s_buzzer_level = -1;

static bool known(int level) { return level == 0 || level == 1; }

bool helmet_io_init(const helmet_io_config_t *config) {
  if (!config) return false;
  s_config = *config;
  if (known(config->sos_active)) {
    gpio_config_t io = {.pin_bit_mask = 1ULL << config->sos_pin,
                        .mode = GPIO_MODE_INPUT,
                        .pull_up_en = config->sos_active == 0,
                        .pull_down_en = config->sos_active == 1};
    if (gpio_config(&io) != ESP_OK) return false;
  }
  if (known(config->led_active)) {
    if (gpio_set_level(config->led_pin, !config->led_active) != ESP_OK ||
        gpio_set_direction(config->led_pin, GPIO_MODE_OUTPUT) != ESP_OK)
      return false;
  }
  if (known(config->buzzer_active) && config->buzzer_hz >= 0) {
    if (config->buzzer_hz == 0) {
      if (gpio_set_level(config->buzzer_pin, !config->buzzer_active) !=
              ESP_OK ||
          gpio_set_direction(config->buzzer_pin, GPIO_MODE_OUTPUT) != ESP_OK)
        return false;
    } else {
      ledc_timer_config_t timer = {.speed_mode = LEDC_LOW_SPEED_MODE,
                                   .duty_resolution = LEDC_TIMER_10_BIT,
                                   .timer_num = LEDC_TIMER_0,
                                   .freq_hz = config->buzzer_hz,
                                   .clk_cfg = LEDC_AUTO_CLK};
      ledc_channel_config_t channel = {
          .gpio_num = config->buzzer_pin,
          .speed_mode = LEDC_LOW_SPEED_MODE,
          .channel = LEDC_CHANNEL_0,
          .timer_sel = LEDC_TIMER_0,
          .flags.output_invert = !config->buzzer_active};
      if (ledc_timer_config(&timer) != ESP_OK ||
          ledc_channel_config(&channel) != ESP_OK)
        return false;
    }
  }
  s_ready = true;
  if (!known(config->sos_active) || !known(config->led_active) ||
      !known(config->buzzer_active) || config->buzzer_hz < 0)
    ESP_LOGW("helmet_io", "BLOCKED: unconfirmed IO channels left unconfigured");
  return true;
}

void helmet_io_set_pattern(helmet_io_pattern_t pattern) {
  s_pattern = pattern <= HELMET_IO_ERROR && pattern >= HELMET_IO_BOOT
                  ? pattern
                  : HELMET_IO_ERROR;
}
void helmet_io_alert(bool emergency, uint32_t now_ms) {
  if (s_alert && s_emergency && !emergency) return;
  s_alert = true;
  s_emergency = emergency;
  s_alert_ms = now_ms;
}
void helmet_io_stop_buzzer(void) { s_alert = false; }

void helmet_io_tick(uint32_t now_ms) {
  if (!s_ready) return;
  if (known(s_config.sos_active)) {
    bool pressed = gpio_get_level(s_config.sos_pin) == s_config.sos_active;
    if (pressed != s_candidate) {
      s_candidate = pressed;
      s_changed_ms = now_ms;
    }
    if (now_ms - s_changed_ms >= 40 && s_pressed != s_candidate) {
      s_pressed = s_candidate;
      if (s_pressed) s_pending = true;
    }
    if (s_pending && event_manager_publish(SYSTEM_EVENT_SOS_PRESSED))
      s_pending = false;
  }
  static const uint16_t periods[] = {1000, 400, 0, 2000, 100, 200, 600};
  uint32_t period = periods[s_pattern];
  bool led_on = period == 0 || now_ms % period < period / 2;
  int level = led_on ? s_config.led_active : !s_config.led_active;
  if (known(s_config.led_active) && level != s_led_level) {
    if (gpio_set_level(s_config.led_pin, level) == ESP_OK) s_led_level = level;
  }
  if (s_alert && !s_emergency && now_ms - s_alert_ms >= 300) s_alert = false;
  bool on = s_alert && (!s_emergency || (now_ms - s_alert_ms) % 600 < 300);
  if (known(s_config.buzzer_active) && s_config.buzzer_hz >= 0 &&
      (int)on != s_buzzer_level) {
    esp_err_t err;
    if (s_config.buzzer_hz == 0) {
      err = gpio_set_level(s_config.buzzer_pin, on ? s_config.buzzer_active
                                                   : !s_config.buzzer_active);
    } else {
      err = ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, on ? 512 : 0);
      if (err == ESP_OK)
        err = ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    }
    if (err == ESP_OK) s_buzzer_level = on;
  }
}
