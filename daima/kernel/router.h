#pragma once

#include "intent.h"
#include "roles.h"

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
    int intent_map[INTENT_COUNT];
    int role_model_map[AGENT_ROLE_COUNT];
} category_router_cfg_t;

category_router_cfg_t category_router_load_and_get_cfg(void);
const daima_category_profile_t *category_router_resolve(enum intent intent);
const daima_category_profile_t *category_router_resolve_for_role(agent_role_t role);
void category_router_reset_for_test(void);
