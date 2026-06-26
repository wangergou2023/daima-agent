/* delegate_task 工具接口 */
#pragma once
#include <stdbool.h>
#include "drivers/tool/tool_types.h"
const struct tool *tool_delegate_definition(void);
const struct tool_driver *tool_delegate_driver(void);
bool tool_delegate_text_has_dsml_markup(const char *text);
