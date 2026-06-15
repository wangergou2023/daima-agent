/* Agent self-diagnosis log tool. */

#pragma once

#include "err.h"
#include "drivers/tool/tool_registry.h"

#include <stddef.h>

err_t tool_daima_log_execute(const char *input_json, char *output, size_t output_size);
const struct tool *tool_daima_log_definition(void);
