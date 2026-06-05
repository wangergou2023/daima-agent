/* Agent self-diagnosis log tool. */

#pragma once

#include "daima_err.h"
#include "tools/tool_registry.h"

#include <stddef.h>

daima_err_t tool_daima_log_execute(const char *input_json, char *output, size_t output_size);
const daima_tool_t *tool_daima_log_definition(void);
