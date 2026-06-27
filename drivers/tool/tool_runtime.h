/* 简化版工具运行时：统一工具输入补丁、执行计时和交互式 sudo 重试。 */

#pragma once

#include <stddef.h>

#include "bus.h"
#include "drivers/llm/llm_proxy.h"
#include "err.h"

typedef struct {
    long elapsed_ms;
    const char *effective_tool_name; /* 若为 NULL，表示仍使用 call->name */
    char *effective_input; /* 若为 NULL，表示仍使用 call->input */
} tool_runtime_result_t;

const struct message *tool_runtime_current_message(void);

err_t tool_runtime_execute_call(const llm_tool_call_t *call,
                                     const struct message *msg,
                                     char *tool_output,
                                     size_t tool_output_size,
                                     tool_runtime_result_t *out_result);
