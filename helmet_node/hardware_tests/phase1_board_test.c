#include "hardware_test_internal.h"

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "test_phase1";

static void phase1_status_task(void *arg) {
  (void)arg;
  uint32_t uptime_seconds = 0;

  while (true) {
    ESP_LOGI(TAG, "PASS heartbeat: uptime=%lus free_heap=%lu bytes",
             (unsigned long)uptime_seconds,
             (unsigned long)esp_get_free_heap_size());
    vTaskDelay(pdMS_TO_TICKS(5000));
    uptime_seconds += 5;
  }
}

bool phase1_board_test_start(void) {
  esp_chip_info_t chip_info = {0};
  uint32_t flash_size = 0;
  esp_chip_info(&chip_info);

  if (esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
    ESP_LOGE(TAG, "FAIL: unable to read flash size");
    return false;
  }

  ESP_LOGI(TAG, "ESP32-S3 bring-up test");
  ESP_LOGI(TAG, "cores=%d revision=%d flash=%luMB free_heap=%lu bytes",
           chip_info.cores, chip_info.revision,
           (unsigned long)(flash_size / (1024U * 1024U)),
           (unsigned long)heap_caps_get_free_size(MALLOC_CAP_8BIT));

  return xTaskCreate(phase1_status_task, "phase1_test", 3072, NULL, 3,
                     NULL) == pdPASS;
}
