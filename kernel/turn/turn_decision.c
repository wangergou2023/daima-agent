/* 单回合决策。 */
#include "turn_decision.h"

#include "intent.h"
#include "router.h"
#include <string.h>

void agent_turn_decision_reset(agent_turn_decision_t *decision)
{
	if (!decision) {
		return;
	}

	memset(decision, 0, sizeof(*decision));
	decision->active_role = AGENT_ROLE_FAST;
}

void agent_turn_decide(struct message *msg, agent_turn_decision_t *decision)
{
	if (!msg || !decision) {
		return;
	}

	intent_gate_classify(msg->content ? msg->content : "", &msg->intent);
	switch (msg->intent) {
	case INTENT_IMPLEMENT:
	case INTENT_FIX:
		decision->active_role = AGENT_ROLE_IMPLEMENT;
		break;
	case INTENT_QA:
	case INTENT_OPEN:
	case INTENT_INVESTIGATE:
	case INTENT_COUNT:
	default:
		decision->active_role = AGENT_ROLE_FAST;
		break;
	}
}

const char *agent_turn_resolve_model(const struct message *msg,
				       agent_role_t active_role)
{
	(void)msg;
	(void)category_router_load_and_get_cfg();
	const category_profile_t *profile =
		category_router_resolve_for_role(active_role);
	return profile ? profile->model : NULL;
}
