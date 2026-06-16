/* 大核调度层接口 */
#pragma once
#include "err.h"

err_t dispatch_execute_tools(const char *tools_json);
err_t dispatch_save_session(const char *chat_id, const char *role, const char *content);
err_t dispatch_compress_context(const char *chat_id);
err_t tool_execute_via_core(const char *name, const char *input, char *output, size_t output_size);