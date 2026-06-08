/* 技能浏览工具。 */

#pragma once

#include "daima_err.h"
#include "tools/tool_registry.h"
#include <stddef.h>

/*
 * 统一技能浏览工具。
 * 输入 JSON：{"action":"list","pattern":"code"}（pattern 可选）
 *          {"action":"view","name":"code-review","file_path":"references/checklist.md"}
 */
daima_err_t tool_skills_execute(const char *input_json, char *output, size_t output_size);
const daima_tool_t *tool_skills_definition(void);
