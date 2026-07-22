/* 单回合决策：intent / role / boss路由 / model。 */

#pragma once

#include <stddef.h>
#include <stdbool.h>

#include "bus.h"
#include "roles.h"
#include "router.h"
#include "registry/registry.h"

typedef struct {
	agent_role_t active_role;

	/* Boss→Specialist 路由决策 */
	bool route_checked;                          /* 是否已完成路由查询 */
	boss_route_action_t route_action;            /* delegate 或 fallback（使用 router.h 定义） */
	char matched_agent_id[AGENT_ID_LEN];         /* 匹配到的 Specialist ID */
	char matched_agent_name[AGENT_NAME_LEN];     /* 匹配到的 Specialist 名称 */
	float match_score;                           /* 匹配分数 */
	agent_definition_t specialist_def;           /* Specialist 的完整定义 */
} agent_turn_decision_t;

void agent_turn_decision_reset(agent_turn_decision_t *decision);
void agent_turn_decide(struct message *msg, agent_turn_decision_t *decision);
const char *agent_turn_resolve_model(const struct message *msg,
				       agent_role_t active_role);
