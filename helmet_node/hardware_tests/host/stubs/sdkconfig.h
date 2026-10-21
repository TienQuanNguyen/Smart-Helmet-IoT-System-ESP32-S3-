#pragma once
#define CONFIG_SMART_HELMET_CLOUD_ENABLE 1
#ifdef HOST_FIREBASE_TEST
#define CONFIG_SMART_HELMET_CLOUD_FIREBASE 1
#define CONFIG_SMART_HELMET_CLOUD_URL "https://example-default-rtdb.asia-southeast1.firebasedatabase.app/"
#define CONFIG_SMART_HELMET_CLOUD_AUTH_TOKEN "test /?&"
#else
#define CONFIG_SMART_HELMET_CLOUD_URL "https://example.invalid/emergency"
#define CONFIG_SMART_HELMET_CLOUD_AUTH_TOKEN ""
#endif
#define CONFIG_SMART_HELMET_CLOUD_QUEUE_LENGTH 4
#define CONFIG_SMART_HELMET_CLOUD_TIMEOUT_MS 5000
#define CONFIG_SMART_HELMET_DEVICE_ID "host-test"
