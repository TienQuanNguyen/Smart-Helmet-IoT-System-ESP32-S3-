#include "power_manager.h"
#include <math.h>
#include <stddef.h>

power_config_t power_manager_default_config(void) {
    return (power_config_t){.source = POWER_DISABLED, .period_ms = 5000,
        .stale_ms = 15000, .read_timeout_ms = 10, .low_pct = 20,
        .critical_pct = 10, .hysteresis_pct = 3, .confirm_samples = 3,
        .minimum_discharge_ma = 10};
}
bool power_config_valid(const power_config_t *c) {
    if (!c || c->source < POWER_DISABLED || c->source > POWER_EXTERNAL ||
        c->period_ms < 1000 || c->period_ms > 60000 ||
        c->stale_ms < c->period_ms || c->stale_ms > 300000 ||
        !c->read_timeout_ms || c->read_timeout_ms > 10 ||
        !c->confirm_samples || c->confirm_samples > 100 ||
        !isfinite(c->low_pct) || !isfinite(c->critical_pct) ||
        !isfinite(c->hysteresis_pct) || c->critical_pct < 0 ||
        c->low_pct <= c->critical_pct || c->hysteresis_pct <= 0 ||
        c->low_pct + c->hysteresis_pct > 100 ||
        !isfinite(c->minimum_discharge_ma) || c->minimum_discharge_ma <= 0 ||
        c->curve_count > POWER_CURVE_MAX ||
        (c->source == POWER_EXTERNAL && !c->read)) return false;
    if (c->capacity_verified && (!isfinite(c->usable_capacity_mah) ||
                                c->usable_capacity_mah <= 0)) return false;
    if (c->curve_verified && c->curve_count < 2) return false;
    for (unsigned i = 0; i < c->curve_count; ++i) {
        power_curve_point_t p = c->curve[i];
        if (!isfinite(p.voltage_v) || p.voltage_v <= 0 ||
            !isfinite(p.soc_pct) || p.soc_pct < 0 || p.soc_pct > 100) return false;
        if (i && (p.voltage_v <= c->curve[i-1].voltage_v ||
                  p.soc_pct <= c->curve[i-1].soc_pct)) return false;
    }
    return true;
}
static bool curve_soc(const power_config_t *c, float voltage, float *soc) {
    if (!c->curve_verified || c->curve_count < 2 ||
        voltage < c->curve[0].voltage_v ||
        voltage > c->curve[c->curve_count-1].voltage_v) return false;
    for (unsigned i = 1; i < c->curve_count; ++i) {
        power_curve_point_t a = c->curve[i-1], b = c->curve[i];
        if (voltage <= b.voltage_v) {
            *soc = a.soc_pct + (voltage-a.voltage_v) *
                   (b.soc_pct-a.soc_pct)/(b.voltage_v-a.voltage_v);
            return true;
        }
    }
    return false;
}
static void invalidate(power_snapshot_t *s) {
    s->voltage_valid = s->current_valid = s->soc_valid = s->runtime_valid = false;
    s->voltage_v = s->current_ma = s->soc_pct = 0;
    s->remaining_minutes = 0;
    s->level = POWER_UNKNOWN;
}
void power_snapshot_expire(power_snapshot_t *s, uint32_t now, uint32_t stale) {
    if (!s) return;
    s->age_ms = now - s->timestamp_ms;
    if (s->status == POWER_OK && s->age_ms > stale) {
        s->status = POWER_STALE;
        invalidate(s);
    }
}
void power_model_update(power_model_t *m, const power_config_t *c,
                        const power_sample_t *raw, bool ok, uint32_t now,
                        power_snapshot_t *s) {
    if (!m || !s) return;
    *s = (power_snapshot_t){.source = c ? c->source : POWER_DISABLED,
                            .status = POWER_UNAVAILABLE,
                            .valid_for_ms = c ? c->stale_ms : 0};
    if (!power_config_valid(c) || c->source == POWER_DISABLED) return;
    s->status = POWER_READ_ERROR;
    if (ok && raw) {
        s->timestamp_ms = raw->timestamp_ms;
        s->age_ms = now - raw->timestamp_ms;
        s->status = s->age_ms > c->stale_ms ? POWER_STALE : POWER_OK;
        if (s->status == POWER_OK) {
            s->voltage_valid = raw->voltage_valid && isfinite(raw->voltage_v) && raw->voltage_v > 0;
            s->current_valid = raw->current_valid && isfinite(raw->current_ma);
            if (s->voltage_valid) s->voltage_v = raw->voltage_v;
            if (s->current_valid) s->current_ma = raw->current_ma;
            if (c->measurement_verified) {
                if (raw->gauge_soc_valid && isfinite(raw->gauge_soc_pct) &&
                    raw->gauge_soc_pct >= 0 && raw->gauge_soc_pct <= 100) {
                    s->soc_pct = raw->gauge_soc_pct;
                    s->soc_valid = true;
                } else if (s->voltage_valid && !raw->gauge_soc_valid) {
                    s->soc_valid = curve_soc(c, s->voltage_v, &s->soc_pct);
                }
            }
            if (!s->voltage_valid && !s->current_valid && !s->soc_valid)
                s->status = POWER_READ_ERROR;
        }
    }
    /* A backend may expose an out-of-order cache entry. Do not count it as a
     * new acquisition or regress the last accepted timestamp (wrap-safe). */
    if (s->status == POWER_OK && m->has_sample &&
        (int32_t)(s->timestamp_ms - m->last_sample_ms) < 0) {
        s->status = POWER_READ_ERROR;
        invalidate(s);
    }
    bool new_sample = s->status == POWER_OK &&
        (!m->has_sample || s->timestamp_ms != m->last_sample_ms);
    if (new_sample) {
        if (m->has_sample && s->timestamp_ms - m->last_sample_ms > c->stale_ms) {
            m->consecutive = m->current_samples = 0;
        }
        m->has_sample = true;
        m->last_sample_ms = s->timestamp_ms;
    }
    if (s->current_valid && c->battery_side_current &&
        s->current_ma >= c->minimum_discharge_ma) {
        if (new_sample) {
            m->average_current_ma = m->current_samples ?
                0.25f * s->current_ma + 0.75f * m->average_current_ma : s->current_ma;
            if (m->current_samples < 3) ++m->current_samples;
        }
    } else {
        m->current_samples = 0;
        m->average_current_ma = 0;
    }
    if (s->soc_valid && c->capacity_verified && c->measurement_verified &&
        m->current_samples >= 3) {
        double minutes = (double)c->usable_capacity_mah * s->soc_pct * 0.6 /
                         m->average_current_ma;
        if (isfinite(minutes) && minutes >= 0 && minutes <= UINT32_MAX) {
            s->runtime_valid = true;
            s->remaining_minutes = (uint32_t)minutes;
        }
    }
    if (!s->soc_valid) {
        m->consecutive = 0;
        m->candidate = POWER_UNKNOWN;
        return;
    }
    if (!new_sample) { s->level = m->stable; return; }
    power_level_t next = POWER_NORMAL;
    if (s->soc_pct <= c->critical_pct ||
        (m->stable == POWER_CRITICAL && s->soc_pct < c->critical_pct + c->hysteresis_pct))
        next = POWER_CRITICAL;
    else if (s->soc_pct <= c->low_pct ||
             ((m->stable == POWER_LOW || m->stable == POWER_CRITICAL) &&
              s->soc_pct < c->low_pct + c->hysteresis_pct)) next = POWER_LOW;
    if (next != m->candidate) { m->candidate = next; m->consecutive = 0; }
    if (m->consecutive < c->confirm_samples) ++m->consecutive;
    if (m->consecutive >= c->confirm_samples) m->stable = next;
    s->level = m->stable;
}
system_event_t power_model_pending_event(const power_model_t *m,
                                         const power_snapshot_t *s) {
    if (!m || !s || !s->soc_valid || s->status != POWER_OK ||
        m->stable == m->delivered) return SYSTEM_EVENT_NONE;
    switch (m->stable) {
    case POWER_LOW: return SYSTEM_EVENT_BATTERY_LOW;
    case POWER_CRITICAL: return SYSTEM_EVENT_BATTERY_CRITICAL;
    case POWER_NORMAL:
        return m->delivered == POWER_LOW || m->delivered == POWER_CRITICAL ?
            SYSTEM_EVENT_BATTERY_RECOVERED : SYSTEM_EVENT_NONE;
    default: return SYSTEM_EVENT_NONE;
    }
}
void power_model_event_delivered(power_model_t *m) { if (m) m->delivered = m->stable; }
bool power_adc_divider_voltage(float mv, float top, float bottom, float max_mv,
                               float *v) {
    if (!v || !isfinite(mv) || !isfinite(top) || !isfinite(bottom) ||
        !isfinite(max_mv) || mv < 0 || max_mv <= 0 || mv > max_mv ||
        top <= 0 || bottom <= 0) return false;
    double result = (double)mv / 1000.0 * (1.0 + (double)top/bottom);
    if (!isfinite(result) || result > 100) return false;
    *v = (float)result;
    return true;
}
const char *power_source_name(power_source_t s) {
    switch (s) { case POWER_MOCK: return "mock"; case POWER_EXTERNAL: return "external";
    default: return "disabled"; }
}
const char *power_status_name(power_status_t s) {
    switch (s) { case POWER_OK: return "ok"; case POWER_READ_ERROR: return "read_error";
    case POWER_STALE: return "stale"; default: return "unavailable"; }
}
const char *power_level_name(power_level_t s) {
    switch (s) { case POWER_NORMAL: return "normal"; case POWER_LOW: return "low";
    case POWER_CRITICAL: return "critical"; default: return "unknown"; }
}
