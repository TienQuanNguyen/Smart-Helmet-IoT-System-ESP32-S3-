#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_adc/adc_oneshot.h"

#define MQ3_DEFAULT_ADC_PIN              4
#define MQ3_DEFAULT_ADC_UNIT             ADC_UNIT_1
#define MQ3_DEFAULT_ADC_CHANNEL          ADC_CHANNEL_3
#define MQ3_DEFAULT_POWER_EN_PIN         5
#define MQ3_DEFAULT_ATTEN                ADC_ATTEN_DB_12
#define MQ3_DEFAULT_ALCOHOL_THRESHOLD_V  1.80f
#define MQ3_DEFAULT_MOCK_ENABLED         false

typedef struct {
    int adc_pin;
    adc_unit_t adc_unit;
    adc_channel_t adc_channel;
    adc_atten_t adc_atten;
    int power_en_pin;
    /* Threshold at the ESP32 ADC pin after the external divider. */
    float alcohol_threshold_voltage;
    bool mock_enabled;
} mq3_config_t;

bool mq3_init(void);
bool mq3_init_with_config(const mq3_config_t *config);
void mq3_power_on(void);
void mq3_power_off(void);
bool mq3_read_raw(uint16_t *adc_raw);
/* Converts raw ADC data to voltage at the ESP32 ADC pin, not MQ-3 AO. */
bool mq3_convert_raw_to_voltage(uint16_t adc_raw, float *adc_node_voltage);
bool mq3_read_voltage(float *adc_node_voltage);
bool mq3_sample_average(float *avg_adc_node_voltage, uint16_t sample_count);
bool mq3_is_alcohol_detected(float adc_node_voltage);
