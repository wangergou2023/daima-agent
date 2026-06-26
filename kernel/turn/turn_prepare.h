/* Turn 准备阶段接口。
 * 负责构建 system prompt、加载对话历史和角色指令，
 * 为 LLM 调用准备好完整的上下文。 */

#pragma once

#include <stddef.h>

#include "bus.h"
#include "cjson.h"
#include "err.h"

/**
 * 准备 turn 所需的完整上下文。
 * 组合 system prompt + 对话历史 + 角色指令，
 * 生成最终发送给 LLM 的 messages JSON 数组。
 * @param msg                入站消息（含 chat_id/channel/intent）
 * @param system_prompt      输出：构建完成的 system prompt
 * @param system_prompt_size 缓冲区大小
 * @param history_json       输出：序列化的对话历史 JSON
 * @param history_json_size  缓冲区大小
 * @param out_messages       输出：最终 messages JSON 数组（调用者需 cJSON_Delete）
 * @return 成功返回 0
 */
err_t agent_turn_prepare(
	const struct message *msg,
	char *system_prompt,
	size_t system_prompt_size,
	char *history_json,
	size_t history_json_size,
	cJSON **out_messages);
