/* Turn 完成/清理阶段接口。
 * 负责回合结束后的收尾工作：追加摘要/提示、清理临时状态、报告错误。
 * 处理取消/超时/错误/工具预算耗尽等异常终止情况。 */

#pragma once

#include <stdbool.h>

#include "bus.h"
#include "err.h"

/**
 * 完成当前 turn 的收尾处理。
 * 追加结束标记（如 incomplete turn 警告）、发送通道回复、
 * 触发 on_finish 钩子、清理临时资源。
 * @param msg                   入站消息（会原地修改回复字段）
 * @param io_final_text         最终文本（可能被追加/修改）
 * @param io_reasoning_text     推理文本
 * @param turn_err              turn 执行错误码（0 表示正常）
 * @param iteration             实际 LLM 迭代次数
 * @param tool_budget_exhausted 是否工具预算耗尽
 * @param cancelled             是否被取消
 */
void agent_turn_finish(
	struct message *msg,
	char **io_final_text,
	char **io_reasoning_text,
	err_t turn_err,
	int iteration,
	bool tool_budget_exhausted,
	bool cancelled);
