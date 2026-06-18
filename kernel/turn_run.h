/* Turn 执行阶段接口（LLM 调用循环）。
 * 负责实际调用 LLM、处理工具调用/返回、迭代对话直到完成。
 * 支持取消令牌、模型回退、工具预算穷尽检测。 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "bus.h"
#include "cjson.h"
#include "err.h"

/**
 * 执行 LLM 工具调用循环（主执行阶段）。
 * 在循环中：发送 messages → 接收 LLM 响应 → 若含 tool_calls 则执行工具 →
 * 将工具结果追加到 messages → 继续下一轮。直至 LLM 返回纯文本或达到限制。
 * @param system_prompt            系统提示词
 * @param messages                 对话历史 JSON（会被原地修改）
 * @param tools_json               工具定义 JSON
 * @param msg                      入站消息（用于上下文/取消检查）
 * @param model_override           模型覆盖（NULL 则使用默认模型）
 * @param cancel_token             取消令牌（用于协作退出检查）
 * @param out_final_text           输出：最终响应文本（调用者需 free）
 * @param out_reasoning_text       输出：推理过程文本（调用者需 free）
 * @param out_iteration            输出：实际迭代次数
 * @param out_tool_budget_exhausted 输出：是否因工具预算耗尽而终止
 * @param out_cancelled            输出：是否因取消而终止
 * @return 成功返回 0
 */
err_t agent_turn_run(
	const char *system_prompt,
	cJSON *messages,
	const char *tools_json,
	const struct message *msg,
	const char *model_override,
	uint64_t cancel_token,
	char **out_final_text,
	char **out_reasoning_text,
	int *out_iteration,
	bool *out_tool_budget_exhausted,
	bool *out_cancelled);
