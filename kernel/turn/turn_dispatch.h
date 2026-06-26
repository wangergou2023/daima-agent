/* 大核调度层接口。
 * 将工具执行和会话保存等重量级操作分发到独立的执行核心（core_task），
 * 通过核间 IPC 实现异步解耦，避免阻塞主 Agent 循环。 */

#pragma once
#include "err.h"

/* 分发工具执行到大核执行器（EXECUTOR 核心） */
err_t dispatch_execute_tools(const char *tools_json);

/* 分发会话保存到内存核心（MEMORY 核心） */
err_t dispatch_save_session(const char *chat_id, const char *role, const char *content);

/* 分发带来源标记的会话保存 */
err_t dispatch_save_session_sourced(const char *chat_id, const char *role,
				     const char *content, const char *source);

/* 分发上下文压缩到内存核心 */
err_t dispatch_compress_context(const char *chat_id);

/* 通过大核执行单个工具调用（同步封装） */
err_t tool_execute_via_core(const char *name, const char *input, char *output, size_t output_size);
