/* 简化版工具运行时：统一工具输入补丁、执行计时和交互式 sudo 重试。 */

#pragma once

#include <stddef.h>

#include "bus.h"
#include "drivers/llm/llm_proxy.h"
#include "err.h"

typedef struct {
    long elapsed_ms;
    char *effective_input; /* 若为 NULL，表示仍使用 call->input */
} daima_tool_runtime_result_t;

daima_err_t tool_runtime_execute_call(const llm_tool_call_t *call,
                                     const daima_msg_t *msg,
                                     char *tool_output,
                                     size_t tool_output_size,
                                     daima_tool_runtime_result_t *out_result);
