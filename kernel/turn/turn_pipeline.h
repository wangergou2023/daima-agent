/* 已准备 turn 的统一执行入口。 */

#pragma once

#include "bus.h"
#include "cjson.h"
#include "turn_exec.h"
#include "turn_decision.h"

/**
 * 执行已经准备好的 turn，并统一触发 finish 钩子和收尾逻辑。
 * @param msg               当前消息对象
 * @param system_prompt     已构建的 system prompt
 * @param messages          已准备好的 messages 数组
 * @param tools_json        当前通道可用工具 JSON
 * @param model_override    主链解析出的模型覆盖
 * @param cancel_chat_id    用于生成 cancel token 的 chat_id
 * @param iteration_offset  追加到本次执行迭代次数上的偏移量
 * @param decision          路由决策（包含 Specialist 信息）
 */
void agent_run_prepared_turn(struct message *msg,
			     char *system_prompt,
			     cJSON *messages,
			     const char *tools_json,
			     const char *model_override,
			     const char *cancel_chat_id,
			     int iteration_offset,
			     const agent_turn_decision_t *decision);

/**
 * 统一执行 turn finish 钩子与收尾处理。
 * @param iteration_offset  追加到本次执行迭代次数上的偏移量
 * @param stats             执行统计
 * @param decision          路由决策
 */
void agent_finalize_turn(struct message *msg,
			 char **io_final_text,
			 char **io_reasoning_text,
			 err_t turn_err,
			 int iteration,
			 bool tool_budget_exhausted,
			 bool cancelled,
			 int iteration_offset,
			 const turn_exec_stats_t *stats,
			 const agent_turn_decision_t *decision);
