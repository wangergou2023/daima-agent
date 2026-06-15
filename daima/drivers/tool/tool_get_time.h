/* 获取时间工具接口。 */

#pragma once

#include "err.h"
#include "drivers/tool/tool_registry.h"
#include <stddef.h>

/**
 * 执行 get_current_time 工具。
 * 通过 HTTP Date 头获取当前时间，设置系统时钟并返回时间字符串。
 */
daima_err_t tool_get_time_execute(const char *input_json, char *output, size_t output_size);
const struct tool *tool_get_time_definition(void);
