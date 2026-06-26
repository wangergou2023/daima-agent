/* Turn 持久化接口。
 * 负责将每轮对话的结果保存到会话存储、排队出站消息、
 * 生成错误回复等持久化和分发操作。 */

#pragma once

#include <stdbool.h>

#include "bus.h"

/* 保存当前 turn 的结果到会话存储（对话历史 + 推理过程） */
void agent_turn_save_session(const struct message *msg, const char *final_text, const char *reasoning, int iteration);

/* 将文本排队为出站消息，失败时可选择释放内存 */
void agent_turn_queue_outbound_text(const struct message *msg, char *text, const char *reasoning, bool free_on_fail);

/* 当工具预算耗尽时生成标准错误回复文本 */
char *agent_turn_build_error_reply(bool tool_budget_exhausted);
