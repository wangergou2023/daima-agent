#pragma once

#include "agent/intent_gate.h"

#include <stdbool.h>

#define CATEGORY_ROUTER_MAX_PROFILES 8

typedef struct {
    char name[32];
    char model[64];
    int context_limit;
    int max_tokens;
} daima_category_profile_t;

typedef struct {
    bool enabled;
    daima_category_profile_t profiles[CATEGORY_ROUTER_MAX_PROFILES];
    int profile_count;
    int intent_map[DAIMA_INTENT_COUNT];
} category_router_cfg_t;

category_router_cfg_t category_router_load_and_get_cfg(void);
const daima_category_profile_t *category_router_resolve(daima_intent_t intent);
void category_router_reset_for_test(void);
