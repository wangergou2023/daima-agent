/* Agent self-diagnosis log tool. */

#pragma once

#include "err.h"
#include "drivers/tool/tool_types.h"

#include <stddef.h>

err_t tool_log_execute(const char *input_json, char *output, size_t output_size);
const struct tool *tool_log_definition(void);
const struct tool_device *tool_log_device(void);
const struct tool_driver *tool_log_driver(void);
