#pragma once

#include <stdbool.h>

const char *daima_env_get(const char *name);
int daima_env_int_or_default(const char *name, int fallback);
bool daima_env_bool_or_default(const char *name, bool fallback);
void daima_env_set(const char *name, const char *value);
