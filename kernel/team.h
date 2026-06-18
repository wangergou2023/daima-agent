/* Team 模式接口。
 * 支持多子 agent 并行协作：将大任务拆分为子任务，
 * 启动多个 EXECUTOR 子 agent 并行执行，最后合并结果注入到主 agent 的 prompt。 */

#pragma once

#include "plan.h"
#include "err.h"

#include <stdbool.h>

#define TEAM_MODE_MAX_SUB_AGENTS 3			/* 最大子 agent 数 */
#define TEAM_MODE_RESULT_MAX 4096			/* 单个子 agent 结果最大长度 */
#define TEAM_MODE_DEFAULT_SUB_AGENT_TIMEOUT_MS 30000	/* 子 agent 默认超时（毫秒） */

/* 单个子 agent 的执行结果 */
typedef struct {
	bool completed;					/* 是否完成（非超时/错误） */
	char result_text[TEAM_MODE_RESULT_MAX];		/* 结果文本 */
	err_t error;					/* 错误码 */
} team_sub_agent_result_t;

/* Team 编排器：管理多子 agent 的并行执行和结果合并 */
typedef struct {
	bool enabled;							/* 是否启用 team 模式 */
	int max_sub_agents;						/* 最大子 agent 数 */
	int sub_agent_timeout_ms;					/* 子 agent 超时阈值 */
	char merged_result[TEAM_MODE_RESULT_MAX * TEAM_MODE_MAX_SUB_AGENTS];	/* 合并结果文本 */
	int completed_count;						/* 完成的子 agent 数 */
} team_orchestrator_t;

/* 编排 team 模式的多子 agent 执行 */
err_t team_mode_orchestrate(const struct plan *plan,
				   const char *system_prompt,
				   const char *tools_json,
				   team_orchestrator_t *out);

/* 将 team 执行结果注入到 system prompt */
err_t team_mode_inject_to_prompt(const team_orchestrator_t *team,
				       char *system_prompt,
				       size_t system_prompt_size);
