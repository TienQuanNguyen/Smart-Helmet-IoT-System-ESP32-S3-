#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "event_manager.h"

#define POWER_CURVE_MAX 16
typedef enum { POWER_DISABLED, POWER_MOCK, POWER_EXTERNAL } power_source_t;
typedef enum { POWER_UNKNOWN, POWER_NORMAL, POWER_LOW, POWER_CRITICAL } power_level_t;
typedef enum { POWER_UNAVAILABLE, POWER_OK, POWER_READ_ERROR, POWER_STALE } power_status_t;
typedef struct { float voltage_v, soc_pct; } power_curve_point_t;
typedef struct {
    uint32_t timestamp_ms;
    bool voltage_valid, current_valid, gauge_soc_valid;
    /* Current: positive discharge at the battery side; negative charge. */
    float voltage_v, current_ma, gauge_soc_pct;
} power_sample_t;
/* Board adapter owns its ADC/I2C handle. Must return within timeout_ms;
 * timestamp is acquisition time, never the time an old cache was read. */
typedef bool (*power_read_fn)(void *context, uint32_t timeout_ms,
                              power_sample_t *sample);
typedef struct {
    power_source_t source;
    uint32_t period_ms, stale_ms, read_timeout_ms;
    float low_pct, critical_pct, hysteresis_pct;
    unsigned confirm_samples;
    /* Opt in only after hardware, calibration and measurement side verified. */
    bool measurement_verified, curve_verified, capacity_verified;
    bool battery_side_current;
    float usable_capacity_mah, minimum_discharge_ma;
    unsigned curve_count;
    power_curve_point_t curve[POWER_CURVE_MAX];
    power_read_fn read;
    void *read_context;
} power_config_t;
typedef struct {
    power_source_t source;
    power_status_t status;
    power_level_t level;
    uint32_t timestamp_ms, age_ms, valid_for_ms;
    bool voltage_valid, current_valid, soc_valid, runtime_valid;
    float voltage_v, current_ma, soc_pct;
    uint32_t remaining_minutes;
} power_snapshot_t;
/* Pure C state, owned by one polling task. */
typedef struct {
    power_level_t stable, candidate, delivered;
    unsigned consecutive, current_samples;
    float average_current_ma;
    uint32_t last_sample_ms;
    bool has_sample;
} power_model_t;

power_config_t power_manager_default_config(void);
bool power_config_valid(const power_config_t *config);
void power_model_update(power_model_t *model, const power_config_t *config,
                        const power_sample_t *sample, bool read_ok,
                        uint32_t now_ms, power_snapshot_t *snapshot);
system_event_t power_model_pending_event(const power_model_t *model,
                                         const power_snapshot_t *snapshot);
void power_model_event_delivered(power_model_t *model);
void power_snapshot_expire(power_snapshot_t *snapshot, uint32_t now_ms,
                            uint32_t stale_ms);
/* Adapter helper: calibrated ADC-node mV + measured divider resistances.
 * No GPIO assignment, ADC unit creation or assumed ADC reference voltage. */
bool power_adc_divider_voltage(float calibrated_mv, float top_ohms,
                               float bottom_ohms, float max_node_mv,
                               float *battery_v);
bool power_mock_read(void *context, uint32_t timeout_ms, power_sample_t *sample);
bool power_manager_init(const power_config_t *config);
bool power_manager_start(void);
bool power_manager_get_snapshot(power_snapshot_t *snapshot);
const char *power_source_name(power_source_t source);
const char *power_status_name(power_status_t status);
const char *power_level_name(power_level_t level);
