/* 简化版 todo 工具。 */

#pragma once

#include "err.h"
#include "drivers/tool/tool_registry.h"
#include <stddef.h>

/*
 * 管理本地待办列表。
 * 支持动作：
 * - list
 * - add {text}
 * - set {items:[{text, done?}]}
 * - update {id, text?, done?}
 * - remove {id}
 * - clear
 */
daima_err_t tool_todo_execute(const char *input_json, char *output, size_t output_size);
const struct tool *tool_todo_definition(void);
