#pragma once
#include <stdint.h>
#ifdef HOST_FIREBASE_TEST
int64_t esp_timer_get_time(void);
#else
static inline int64_t esp_timer_get_time(void) { return 0; }
#endif
