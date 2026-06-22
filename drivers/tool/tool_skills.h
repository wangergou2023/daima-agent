/* 技能浏览工具。 */

#pragma once

#include "err.h"
#include "drivers/tool/tool_types.h"
#include <stddef.h>

/*
 * 统一技能浏览工具。
 * 输入 JSON：{"action":"list","pattern":"code"}（pattern 可选）
 *          {"action":"view","name":"code-review","file_path":"references/checklist.md"}
 */
err_t tool_skills_execute(const char *input_json, char *output, size_t output_size);
const struct tool *tool_skills_definition(void);
const struct tool_device *tool_skills_device(void);
const struct tool_driver *tool_skills_driver(void);
