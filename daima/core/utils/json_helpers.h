#pragma once

#include "cJSON.h"

const char *json_string(cJSON *obj, const char *key);
int json_number(cJSON *obj, const char *key);
const char *json_string_or_default(const cJSON *obj, const char *key, const char *fallback);
