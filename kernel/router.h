#pragma once

#include "intent.h"
#include "roles.h"
#include "registry/registry.h"

#include <stdbool.h>

#define CATEGORY_ROUTER_MAX_PROFILES 8

typedef struct {
    char name[32];
    char model[64];
    int context_limit;
    int max_tokens;
} category_profile_t;

typedef struct {
    category_profile_t profiles[CATEGORY_ROUTER_MAX_PROFILES];
    int profile_count;
    int role_model_map[AGENT_ROLE_COUNT];
} category_router_cfg_t;

category_router_cfg_t category_router_load_and_get_cfg(void);
const category_profile_t *category_router_resolve_for_role(agent_role_t role);
void category_router_reset_for_test(void);

/* ──── Boss 路由决策 ──── */

#define BOSS_ROUTE_TARGET_LEN  AGENT_ID_LEN

typedef enum {
    BOSS_ROUTE_DELEGATE = 0,
    BOSS_ROUTE_FALLBACK,
} boss_route_action_t;

typedef struct {
    boss_route_action_t action;
    char target_agent_id[BOSS_ROUTE_TARGET_LEN];
    char target_agent_name[AGENT_NAME_LEN];
    float match_score;
    agent_definition_t target_agent_def;
    char reason[256];
} boss_routing_decision_t;

err_t boss_route_task(const task_analysis_t *analysis,
                      boss_routing_decision_t *out_decision);

err_t boss_get_fallback_profile(agent_definition_t *out_profile);
