#pragma once

#include <stdbool.h>
#include <stdint.h>

#define MQ3_DEFAULT_ADC_UNIT             1
#define MQ3_DEFAULT_ADC_CHANNEL          3
#define MQ3_DEFAULT_POWER_EN_PIN         5
#define MQ3_DEFAULT_ATTEN                3
#define MQ3_DEFAULT_ALCOHOL_THRESHOLD_V  1.80f
#define MQ3_DEFAULT_MOCK_ENABLED         false

typedef struct {
    int adc_unit;
    int adc_channel;
    int adc_atten;
    int power_en_pin;
    float alcohol_threshold_voltage;
    bool mock_enabled;
} mq3_config_t;

bool mq3_init(void);
bool mq3_init_with_config(const mq3_config_t *config);
void mq3_power_on(void);
void mq3_power_off(void);
bool mq3_read_raw(uint16_t *adc_raw);
bool mq3_read_voltage(float *voltage);
bool mq3_sample_average(float *avg_voltage, uint16_t sample_count);
bool mq3_is_alcohol_detected(float voltage);
