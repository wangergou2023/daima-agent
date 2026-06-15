#pragma once

#include <stdbool.h>

const char *env_get(const char *name);
int env_int_or_default(const char *name, int fallback);
bool env_bool_or_default(const char *name, bool fallback);
void env_set(const char *name, const char *value);
