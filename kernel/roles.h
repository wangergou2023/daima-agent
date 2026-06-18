/* Agent 角色定义与映射接口。
 * 定义 4 种 agent 角色（FAST/PLANNER/EXECUTOR/REVIEWER），
 * 每种角色有独立的 prompt 后缀和分类标签。
 * 提供 intent → 角色数组的映射，供调度器决定需要启动哪些 agent。 */

#pragma once

#include "intent.h"

/* Agent 角色枚举：决定 agent 的行为模式和能力边界 */
typedef enum {
	AGENT_ROLE_FAST = 0,	/* 快速模式：轻量级 LLM，处理简单问答 */
	AGENT_ROLE_PLANNER,	/* 规划者：生成分步执行计划 */
	AGENT_ROLE_EXECUTOR,	/* 执行者：按计划实现代码 */
	AGENT_ROLE_REVIEWER,	/* 审查者：评审执行结果 */
	AGENT_ROLE_COUNT	/* 角色总数（用于数组边界） */
} agent_role_t;

/* 获取角色人类可读名称 */
const char *agent_role_name(agent_role_t role);

/* 获取角色的 prompt 后缀（追加到 system prompt 的角色指令） */
const char *agent_role_prompt_suffix(agent_role_t role);

/* 获取角色的分类标签（如 "planner", "executor"） */
const char *agent_role_category(agent_role_t role);

/**
 * 根据意图查询需要启动的角色列表。
 * 不同意图可能触发不同数量和类型的 agent（如 IMPLEMENT → PLANNER+EXECUTOR+REVIEWER）。
 * @param intent     用户消息意图
 * @param roles_out  输出：角色数组（最多 3 个）
 * @return 实际角色数量
 */
int agent_roles_for_intent(enum intent intent, agent_role_t roles_out[3]);
