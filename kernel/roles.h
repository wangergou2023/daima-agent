/* Agent 角色定义与映射接口。 */

#pragma once

#include "intent.h"

typedef enum {
	AGENT_ROLE_FAST = 0,
	AGENT_ROLE_ORACLE,
	AGENT_ROLE_IMPLEMENT,
	AGENT_ROLE_COUNT	/* 角色总数（用于数组边界） */
} agent_role_t;

const char *agent_role_name(agent_role_t role);
const char *agent_role_prompt_suffix(agent_role_t role);
