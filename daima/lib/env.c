#include "env.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

const char *env_get(const char *name)
{
    if (!name || !name[0]) {
        return NULL;
    }
    const char *raw = getenv(name);
    if (raw && raw[0]) {
        return raw;
    }
    return NULL;
}

int env_int_or_default(const char *name, int fallback)
{
    const char *raw = env_get(name);
    if (!raw) {
        return fallback;
    }
    char *end = NULL;
    long val = strtol(raw, &end, 10);
    if (end == raw || (end && *end != '\0')) {
        return fallback;
    }
    return (int)val;
}

bool env_bool_or_default(const char *name, bool fallback)
{
    const char *raw = env_get(name);
    if (!raw) {
        return fallback;
    }
    if (strcmp(raw, "1") == 0 || strcasecmp(raw, "true") == 0 || strcasecmp(raw, "yes") == 0) {
        return true;
    }
    if (strcmp(raw, "0") == 0 || strcasecmp(raw, "false") == 0 || strcasecmp(raw, "no") == 0) {
        return false;
    }
    return fallback;
}

void env_set(const char *name, const char *value)
{
    if (!name || !name[0] || !value || !value[0]) {
        return;
    }
    setenv(name, value, 1);
}
