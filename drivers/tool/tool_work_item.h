/* Work item collection tool. */

#pragma once

#include "err.h"
#include "drivers/tool/tool_registry.h"

#include <stddef.h>

err_t tool_work_item_execute(const char *input_json, char *output, size_t output_size);
const struct tool *tool_work_item_definition(void);
const struct tool_device *tool_work_item_device(void);
const struct tool_driver *tool_work_item_driver(void);

