#pragma once
#include "cJSON.h"
#include "power_manager.h"
/* Owns only the added child; caller owns root. Unknown numbers become JSON null. */
bool power_json_add(cJSON *root, const power_snapshot_t *snapshot);
