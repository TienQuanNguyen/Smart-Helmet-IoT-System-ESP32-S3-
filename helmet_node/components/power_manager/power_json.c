#include "power_json.h"
#include <math.h>

static bool number(cJSON *o, const char *key, bool valid, double value) {
    return valid && isfinite(value) ? cJSON_AddNumberToObject(o, key, value) != NULL :
                                     cJSON_AddNullToObject(o, key) != NULL;
}
bool power_json_add(cJSON *root, const power_snapshot_t *s) {
    if (!root || !s) return false;
    cJSON *p = cJSON_AddObjectToObject(root, "power");
    bool fresh = s->status == POWER_OK;
    bool v = fresh && s->voltage_valid && isfinite(s->voltage_v) && s->voltage_v > 0;
    bool i = fresh && s->current_valid && isfinite(s->current_ma);
    bool soc = fresh && s->soc_valid && isfinite(s->soc_pct) && s->soc_pct >= 0 && s->soc_pct <= 100;
    bool runtime = soc && s->runtime_valid;
    return p && cJSON_AddNumberToObject(p, "schema", 1) &&
        cJSON_AddStringToObject(p, "source", power_source_name(s->source)) &&
        cJSON_AddBoolToObject(p, "mock", s->source == POWER_MOCK) &&
        cJSON_AddStringToObject(p, "status", power_status_name(s->status)) &&
        cJSON_AddStringToObject(p, "level", power_level_name(soc ? s->level : POWER_UNKNOWN)) &&
        cJSON_AddNumberToObject(p, "age_ms", s->age_ms) &&
        cJSON_AddBoolToObject(p, "voltage_valid", v) &&
        cJSON_AddBoolToObject(p, "current_valid", i) &&
        cJSON_AddBoolToObject(p, "soc_valid", soc) &&
        cJSON_AddBoolToObject(p, "runtime_valid", runtime) &&
        number(p, "voltage_v", v, s->voltage_v) &&
        number(p, "current_ma", i, s->current_ma) &&
        number(p, "soc_pct", soc, s->soc_pct) &&
        number(p, "remaining_minutes", runtime, s->remaining_minutes);
}
