/* delegate_task 工具接口 */
#pragma once

#include <stddef.h>

#include "drivers/tool/tool_types.h"
#include "drivers/tool/tool_delegate_lifecycle.h"

const struct tool *tool_delegate_definition(void);
const struct tool_driver *tool_delegate_driver(void);
int tool_delegate_next_seq(void);
int tool_delegate_current_seq(void);
