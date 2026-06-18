/* 调试工具接口。
 * 提供 prompt 快照导出功能，用于开发调试时检查实际发送给 LLM 的完整上下文。 */

#pragma once

#include "bus.h"

/**
 * 导出当前 turn 的完整 prompt 快照到调试文件。
 * 包含 system prompt、对话历史、工具定义等。
 * @param msg           入站消息（用于确定输出文件名）
 * @param system_prompt 当前系统提示词
 */
void agent_prompt_dump_snapshot(const struct message *msg, const char *system_prompt);
