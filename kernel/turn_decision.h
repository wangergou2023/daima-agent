/* 单回合决策：intent / role / plan / model。 */

#pragma once

#include <stddef.h>

#include "bus.h"
#include "plan.h"
#include "roles.h"

typedef struct {
	struct plan plan;
	agent_role_t active_role;
} agent_turn_decision_t;

void agent_turn_decision_reset(agent_turn_decision_t *decision);
void agent_turn_decide(struct message *msg, agent_turn_decision_t *decision);
const char *agent_turn_resolve_model(const struct message *msg,
				       agent_role_t active_role);
