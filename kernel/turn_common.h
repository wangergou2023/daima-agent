/* Turn 流水线公共工具函数。
 * 提供环境变量读取、消息来源判断、chat_id 转换等各阶段共用的辅助函数。 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "bus.h"

/* 读取环境变量整数值，不存在时返回 fallback */
int agent_env_int_or_default(const char *name, int fallback);

/* 读取环境变量布尔值，不存在时返回 fallback */
bool agent_env_bool_or_default(const char *name, bool fallback);

/* 获取消息来源通道名，无 channel 时返回 "unknown" */
const char *agent_msg_source_or_default(const struct message *msg);

/* 判断消息是否为内部控制指令（如 system/cron），非用户消息 */
bool agent_msg_is_internal_control(const struct message *msg);

/* 判断消息是否为合成事件（如定时器触发、自动截断），非人工输入 */
bool agent_msg_is_synthetic_event(const struct message *msg);

/* 获取当前 turn 中消息的对话角色（user/assistant/system） */
const char *agent_msg_role_for_current_turn(const struct message *msg);

/* 获取入站消息的会话角色标签 */
const char *agent_session_role_for_inbound_msg(const struct message *msg);

/* 将 chat_id 转换为文件系统安全的 slug 名 */
void agent_chat_id_to_slug(const char *chat_id, char *buf, size_t size);

/* 清理入站消息的临时字段 */
void agent_cleanup_inbound_msg(struct message *msg);

/* 清理出站消息的临时字段 */
void agent_cleanup_outbound_msg(struct message *msg);
