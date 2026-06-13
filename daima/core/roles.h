#pragma once

#include "core/intent_gate.h"

typedef enum {
    AGENT_ROLE_FAST = 0,
    AGENT_ROLE_PLANNER,
    AGENT_ROLE_EXECUTOR,
    AGENT_ROLE_REVIEWER,
    AGENT_ROLE_COUNT
} agent_role_t;

const char *agent_role_name(agent_role_t role);
const char *agent_role_prompt_suffix(agent_role_t role);
const char *agent_role_category(agent_role_t role);

int agent_roles_for_intent(daima_intent_t intent, agent_role_t roles_out[3]);
