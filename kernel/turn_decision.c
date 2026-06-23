/* 单回合决策。 */
#include "turn_decision.h"

#include "intent.h"
#include "router.h"
#include <string.h>

typedef struct {
	agent_role_t roles[3];
	int role_count;
} agent_turn_role_set_t;

static agent_role_t active_role_for_plan(const struct plan *plan,
					 const agent_turn_role_set_t *role_set)
{
	if (!role_set || role_set->role_count <= 0) {
		return AGENT_ROLE_FAST;
	}
	if (plan && plan->has_plan && plan->reviewed && role_set->role_count > 1) {
		return role_set->roles[1];
	}
	return role_set->roles[0];
}

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
	agent_turn_role_set_t role_set = {0};

	if (!msg || !decision) {
		return;
	}

	intent_gate_classify(msg->content ? msg->content : "", &msg->intent);
	role_set.role_count = agent_roles_for_intent(msg->intent, role_set.roles);
	if (msg->intent == INTENT_IMPLEMENT || msg->intent == INTENT_FIX) {
		(void)plan_review_generate(msg->intent, msg->content, "", &decision->plan);
	}
	decision->active_role = active_role_for_plan(&decision->plan, &role_set);
}

const char *agent_turn_resolve_model(const struct message *msg,
				       agent_role_t active_role)
{
	category_router_cfg_t cfg = category_router_load_and_get_cfg();
	if (!cfg.enabled) {
		return NULL;
	}

	const category_profile_t *profile =
		category_router_resolve_for_role(active_role);
	if (!profile) {
		profile = category_router_resolve(msg ? msg->intent : INTENT_OPEN);
	}
	return profile ? profile->model : NULL;
}
