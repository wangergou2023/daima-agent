/* 技能浏览工具。 */

#pragma once

#include "daima_err.h"
#include "tools/tool_registry.h"
#include <stddef.h>

/*
 * 列出已安装技能。
 * 输入 JSON：{"pattern":"code"}（pattern 可选）
 */
daima_err_t tool_skills_list_execute(const char *input_json, char *output, size_t output_size);
const daima_tool_t *tool_skills_list_definition(void);

/*
 * 查看技能内容或技能目录中的关联文件。
 * 输入 JSON：{"name":"code-review","file_path":"references/checklist.md"}
 * - file_path 可选；省略时返回主文件 SKILL.md
 */
daima_err_t tool_skill_view_execute(const char *input_json, char *output, size_t output_size);
const daima_tool_t *tool_skill_view_definition(void);
