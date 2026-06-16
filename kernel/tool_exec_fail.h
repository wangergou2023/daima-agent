/* 工具执行失败观察器接口 */
#pragma once
#include "err.h"
struct message;
void log_tool_payload_preview(const char *phase, const struct message *msg,
                               const char *tool_name, const char *tool_id,
                               const char *input, const char *output, err_t err);
void collect_tool_failure_work_item(const struct message *msg,
                                     const char *tool_name, const char *tool_input,
                                     const char *tool_output, err_t tool_err);