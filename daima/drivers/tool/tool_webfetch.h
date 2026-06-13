/* Web fetch tool. */

#pragma once

#include "err.h"
#include "drivers/tool/tool_registry.h"

#include <stddef.h>

daima_err_t tool_webfetch_execute(const char *input_json, char *output, size_t output_size);
const daima_tool_t *tool_webfetch_definition(void);
