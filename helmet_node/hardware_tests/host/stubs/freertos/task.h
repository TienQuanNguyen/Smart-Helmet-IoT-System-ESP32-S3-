#pragma once
#include <stddef.h>
typedef void *TaskHandle_t;
static inline TaskHandle_t xTaskGetCurrentTaskHandle(void) { return NULL; }
#ifdef HOST_CLOUD_TEST
void vTaskDelay(unsigned ticks);
#else
static inline void vTaskDelay(unsigned ticks) { (void)ticks; }
#endif
static inline int xTaskCreate(void (*fn)(void *), const char *name, unsigned stack,
                              void *arg, unsigned priority, TaskHandle_t *handle) {
  (void)fn; (void)name; (void)stack; (void)arg; (void)priority; (void)handle;
#ifdef HOST_CLOUD_TEST
  return 1;
#else
  return 0;
#endif
}
