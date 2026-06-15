/* Work item collection tool. */

#pragma once

#include "err.h"
#include "drivers/tool/tool_registry.h"

#include <stddef.h>

daima_err_t tool_work_item_execute(const char *input_json, char *output, size_t output_size);
const struct tool *tool_work_item_definition(void);

