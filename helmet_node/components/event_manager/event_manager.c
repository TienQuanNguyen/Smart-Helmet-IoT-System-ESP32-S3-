#include "event_manager.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define EVENT_MANAGER_QUEUE_LENGTH 16
#define EVENT_MANAGER_QUEUE_ITEM_SIZE sizeof(system_event_t)

static const char *TAG = "event_manager";

static QueueHandle_t s_event_queue;

bool event_manager_init(void) {
  if (s_event_queue != NULL) {
    return true;
  }

  s_event_queue =
      xQueueCreate(EVENT_MANAGER_QUEUE_LENGTH, EVENT_MANAGER_QUEUE_ITEM_SIZE);
  if (s_event_queue == NULL) {
    ESP_LOGE(TAG, "Failed to create event queue");
    return false;
  }

  ESP_LOGI(TAG, "Event manager initialized");
  return true;
}

bool event_manager_publish(system_event_t event) {
  if (s_event_queue == NULL) {
    ESP_LOGE(TAG, "Event queue is not initialized");
    return false;
  }

  return xQueueSend(s_event_queue, &event, 0) == pdTRUE;
}

bool event_manager_wait(system_event_t *event, uint32_t timeout_ms) {
  if (event == NULL) {
    ESP_LOGE(TAG, "Event output is NULL");
    return false;
  }

  if (s_event_queue == NULL) {
    ESP_LOGE(TAG, "Event queue is not initialized");
    return false;
  }

  TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
  return xQueueReceive(s_event_queue, event, timeout_ticks) == pdTRUE;
}
