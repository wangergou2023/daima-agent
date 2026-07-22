/* 单回合决策：intent → role → Boss路由查询。 */
#include "turn_decision.h"

#include "intent.h"
#include "router.h"
#include "linux/printk.h"
#include "linux/kernel.h"
#include <string.h>

void agent_turn_decision_reset(agent_turn_decision_t *decision)
{
	if (!decision) {
		return;
	}

	memset(decision, 0, sizeof(*decision));
	decision->active_role = AGENT_ROLE_FAST;
	decision->route_action = BOSS_ROUTE_FALLBACK;
}

void agent_turn_decide(struct message *msg, agent_turn_decision_t *decision)
{
	if (!msg || !decision) {
		return;
	}

	/* 步骤1: 意图分类 → 角色 */
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

	/* 步骤2: Boss 路由决策 — 查询 Agent Registry 是否有匹配的 Specialist */
	task_analysis_t analysis;
	memset(&analysis, 0, sizeof(analysis));
	err_t err = intent_gate_analyze_task(msg->content ? msg->content : "", &analysis);
	if (err == 0 && analysis.capability_tags[0]) {
		boss_routing_decision_t route;
		memset(&route, 0, sizeof(route));
		err = boss_route_task(&analysis, &route);
		if (err == 0) {
			decision->route_checked = true;
			if (route.action == BOSS_ROUTE_DELEGATE) {
				decision->route_action = BOSS_ROUTE_DELEGATE;
				strscpy(decision->matched_agent_id, route.target_agent_id,
					sizeof(decision->matched_agent_id));
				strscpy(decision->matched_agent_name, route.target_agent_name,
					sizeof(decision->matched_agent_name));
				decision->match_score = route.match_score;
				decision->specialist_def = route.target_agent_def;
				pr_info("Boss route DELEGATE: %s (score=%.2f) reason=%s",
					route.target_agent_name,
					(double)route.match_score,
					route.reason);
			} else {
				decision->route_action = BOSS_ROUTE_FALLBACK;
				pr_info("Boss route FALLBACK: %s", route.reason);
			}
		}
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
