#pragma once

#include "roles.h"
#include "plan.h"

#include <stdbool.h>

void agent_extension_state_reset(void);
daima_plan_t *agent_extension_state_plan(void);
bool agent_extension_state_has_reviewed_plan(void);
void agent_extension_state_set_roles(const agent_role_t *roles, int role_count, agent_role_t active_role);
int agent_extension_state_role_count(void);
agent_role_t agent_extension_state_active_role(void);
