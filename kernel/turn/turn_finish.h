/* Turn 完成/清理阶段接口。
 * 负责回合结束后的收尾工作：追加摘要/提示、清理临时状态、报告错误。
 * 处理取消/超时/错误/工具预算耗尽等异常终止情况。 */

#pragma once

#include <stdbool.h>

#include "bus.h"
#include "err.h"
#include "turn_exec.h"
#include "turn_decision.h"

/**
 * 完成当前 turn 的收尾处理。
 * @param msg                   入站消息
 * @param io_final_text         最终文本
 * @param io_reasoning_text     推理文本
 * @param turn_err              turn 执行错误码
 * @param iteration             实际 LLM 迭代次数
 * @param tool_budget_exhausted 是否工具预算耗尽
 * @param cancelled             是否被取消
 * @param stats                 执行统计（供 Transcript 使用）
 * @param decision              路由决策（供 Transcript 使用）
 */
void agent_turn_finish(
	struct message *msg,
	char **io_final_text,
	char **io_reasoning_text,
	err_t turn_err,
	int iteration,
	bool tool_budget_exhausted,
	bool cancelled,
	const turn_exec_stats_t *stats,
	const agent_turn_decision_t *decision);
