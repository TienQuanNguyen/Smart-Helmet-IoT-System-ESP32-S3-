#pragma once
typedef void *SemaphoreHandle_t;
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
static inline int xSemaphoreTake(SemaphoreHandle_t lock, unsigned timeout) {
  (void)lock; (void)timeout; return 1;
}
static inline int xSemaphoreGive(SemaphoreHandle_t lock) { (void)lock; return 1; }
static inline void vSemaphoreDelete(SemaphoreHandle_t lock) { (void)lock; }
