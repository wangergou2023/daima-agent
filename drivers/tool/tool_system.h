/* 系统命令工具接口。 */

#pragma once

#include "err.h"
#include "drivers/tool/tool_registry.h"
#include <stddef.h>

/**
 * 本地 terminal 工具：执行 shell 命令并返回结构化结果。
 * 输入 JSON：
 *   {"command":"<shell command>","timeout":120,"workdir":"/abs/path","sudo_password":"..."}
 * 兼容字段：
 *   {"cmd":"<shell command>"}
 */
err_t tool_terminal_execute(const char *input_json, char *output, size_t output_size);

/**
 * 获取 terminal 工具定义。
 * - 让 schema/description 跟执行逻辑放在同一模块，减少 registry 中的大段内联文本
 */
const struct tool *tool_terminal_definition(void);
const struct tool_device *tool_terminal_device(void);
const struct tool_driver *tool_terminal_driver(void);
