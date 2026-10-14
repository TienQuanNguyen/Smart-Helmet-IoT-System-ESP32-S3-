#include "hardware_test_internal.h"

#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define OLED_I2C_PORT I2C_NUM_0
#define OLED_I2C_FREQUENCY_HZ 100000
#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_PAGE_COUNT (OLED_HEIGHT / 8)
#define OLED_FRAMEBUFFER_SIZE (OLED_WIDTH * OLED_PAGE_COUNT)
#define OLED_CONTROL_COMMAND 0x00
#define OLED_CONTROL_DATA 0x40

static const char *TAG = "test_oled_i2c";
static uint8_t s_oled_address;
static uint8_t s_framebuffer[OLED_FRAMEBUFFER_SIZE];

typedef struct {
  char character;
  uint8_t columns[5];
} oled_glyph_t;

static const oled_glyph_t GLYPHS[] = {
    {'I', {0x00, 0x41, 0x7F, 0x41, 0x00}},
    {'2', {0x42, 0x61, 0x51, 0x49, 0x46}},
    {'C', {0x3E, 0x41, 0x41, 0x41, 0x22}},
    {'O', {0x3E, 0x41, 0x41, 0x41, 0x3E}},
    {'K', {0x7F, 0x08, 0x14, 0x22, 0x41}},
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00}},
};

static esp_err_t oled_write(uint8_t control, const uint8_t *data,
                            size_t length) {
  uint8_t buffer[17] = {0};
  if ((data == NULL) || (length == 0) || (length > 16)) {
    return ESP_ERR_INVALID_ARG;
  }

  buffer[0] = control;
  memcpy(&buffer[1], data, length);
  return i2c_master_write_to_device(OLED_I2C_PORT, s_oled_address, buffer,
                                    length + 1, pdMS_TO_TICKS(100));
}

static esp_err_t oled_command(uint8_t command) {
  return oled_write(OLED_CONTROL_COMMAND, &command, 1);
}

static esp_err_t oled_probe(uint8_t address) {
  i2c_cmd_handle_t command = i2c_cmd_link_create();
  if (command == NULL) {
    return ESP_ERR_NO_MEM;
  }

  i2c_master_start(command);
  i2c_master_write_byte(command, (address << 1) | I2C_MASTER_WRITE, true);
  i2c_master_stop(command);
  esp_err_t result =
      i2c_master_cmd_begin(OLED_I2C_PORT, command, pdMS_TO_TICKS(50));
  i2c_cmd_link_delete(command);
  return result;
}

static bool oled_bus_init(const hardware_test_config_t *config) {
  const i2c_config_t bus_config = {
      .mode = I2C_MODE_MASTER,
      .sda_io_num = (gpio_num_t)config->i2c_sda_pin,
      .scl_io_num = (gpio_num_t)config->i2c_scl_pin,
      .sda_pullup_en = GPIO_PULLUP_ENABLE,
      .scl_pullup_en = GPIO_PULLUP_ENABLE,
      .master.clk_speed = OLED_I2C_FREQUENCY_HZ,
      .clk_flags = 0,
  };

  esp_err_t result = i2c_param_config(OLED_I2C_PORT, &bus_config);
  if (result == ESP_OK) {
    result = i2c_driver_install(OLED_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
  }
  if ((result != ESP_OK) && (result != ESP_ERR_INVALID_STATE)) {
    ESP_LOGE(TAG, "I2C init failed: %s", esp_err_to_name(result));
    return false;
  }

  ESP_LOGI(TAG, "I2C levels: SDA(GPIO%d)=%d SCL(GPIO%d)=%d",
           config->i2c_sda_pin,
           gpio_get_level((gpio_num_t)config->i2c_sda_pin),
           config->i2c_scl_pin,
           gpio_get_level((gpio_num_t)config->i2c_scl_pin));

  if (oled_probe(0x3C) == ESP_OK) {
    s_oled_address = 0x3C;
  } else if (oled_probe(0x3D) == ESP_OK) {
    s_oled_address = 0x3D;
  } else {
    ESP_LOGE(TAG, "No SSD1306 ACK at 0x3C or 0x3D");
    return false;
  }

  ESP_LOGI(TAG, "OLED found at 0x%02X", s_oled_address);
  return true;
}

static bool oled_controller_init(void) {
  static const uint8_t commands[] = {
      0xAE,       /* display off */
      0xD5, 0x80, /* clock divide */
      0xA8, 0x3F, /* multiplex 1/64 */
      0xD3, 0x00, /* display offset */
      0x40,       /* start line */
      0x8D, 0x14, /* charge pump */
      0x20, 0x00, /* horizontal addressing */
      0xA1,       /* segment remap */
      0xC8,       /* COM scan direction */
      0xDA, 0x12, /* COM pins for 128x64 */
      0x81, 0x7F, /* contrast */
      0xD9, 0xF1, /* pre-charge */
      0xDB, 0x40, /* VCOM detect */
      0xA4,       /* display follows RAM */
      0xA6,       /* normal display */
      0x2E,       /* deactivate scroll */
      0xAF,       /* display on */
  };

  for (size_t index = 0; index < sizeof(commands); ++index) {
    if (oled_command(commands[index]) != ESP_OK) {
      ESP_LOGE(TAG, "SSD1306 init failed at command index %u",
               (unsigned)index);
      return false;
    }
  }
  return true;
}

static void oled_set_pixel(int x, int y) {
  if ((x >= 0) && (x < OLED_WIDTH) && (y >= 0) && (y < OLED_HEIGHT)) {
    s_framebuffer[x + ((y / 8) * OLED_WIDTH)] |= (uint8_t)(1U << (y % 8));
  }
}

static void oled_draw_border(void) {
  for (int x = 0; x < OLED_WIDTH; ++x) {
    oled_set_pixel(x, 0);
    oled_set_pixel(x, OLED_HEIGHT - 1);
  }
  for (int y = 0; y < OLED_HEIGHT; ++y) {
    oled_set_pixel(0, y);
    oled_set_pixel(OLED_WIDTH - 1, y);
  }
}

static const uint8_t *oled_find_glyph(char character) {
  for (size_t index = 0; index < sizeof(GLYPHS) / sizeof(GLYPHS[0]); ++index) {
    if (GLYPHS[index].character == character) {
      return GLYPHS[index].columns;
    }
  }
  return GLYPHS[5].columns;
}

static void oled_draw_text_2x(int x, int y, const char *text) {
  while ((text != NULL) && (*text != '\0')) {
    const uint8_t *glyph = oled_find_glyph(*text++);
    for (int column = 0; column < 5; ++column) {
      for (int row = 0; row < 7; ++row) {
        if ((glyph[column] & (1U << row)) != 0) {
          oled_set_pixel(x + column * 2, y + row * 2);
          oled_set_pixel(x + column * 2 + 1, y + row * 2);
          oled_set_pixel(x + column * 2, y + row * 2 + 1);
          oled_set_pixel(x + column * 2 + 1, y + row * 2 + 1);
        }
      }
    }
    x += 12;
  }
}

static bool oled_flush(void) {
  const uint8_t setup[] = {0x21, 0x00, OLED_WIDTH - 1,
                           0x22, 0x00, OLED_PAGE_COUNT - 1};
  for (size_t index = 0; index < sizeof(setup); ++index) {
    if (oled_command(setup[index]) != ESP_OK) {
      return false;
    }
  }

  for (size_t offset = 0; offset < sizeof(s_framebuffer); offset += 16) {
    if (oled_write(OLED_CONTROL_DATA, &s_framebuffer[offset], 16) != ESP_OK) {
      return false;
    }
  }
  return true;
}

bool i2c_oled_test_start(const hardware_test_config_t *config) {
  if (!oled_bus_init(config) || !oled_controller_init()) {
    return false;
  }

  memset(s_framebuffer, 0, sizeof(s_framebuffer));
  oled_draw_border();
  oled_draw_text_2x(34, 25, "I2C OK");
  if (!oled_flush()) {
    ESP_LOGE(TAG, "Failed to send framebuffer");
    return false;
  }

  ESP_LOGI(TAG,
           "FRAMEBUFFER SENT: visually confirm border and I2C OK before "
           "recording HARDWARE PASS");
  return true;
}
