/* 天气工具接口。 */

#pragma once

#include "err.h"
#include "drivers/tool/tool_registry.h"
#include <stddef.h>

/**
 * 执行天气工具。
 * 输入 JSON：{"location":"Beijing", "type":"current|forecast", "days":1}
 */
daima_err_t tool_weather_execute(const char *input_json, char *output, size_t output_size);

/**
 * 初始化天气工具（wttr.in；无需 API Key）。
 */
daima_err_t tool_weather_init(void);
const struct tool *tool_weather_definition(void);
