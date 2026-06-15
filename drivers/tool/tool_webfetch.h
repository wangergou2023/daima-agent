/* Web fetch tool. */

#pragma once

#include "err.h"
#include "drivers/tool/tool_registry.h"

#include <stddef.h>

err_t tool_webfetch_execute(const char *input_json, char *output, size_t output_size);
const struct tool *tool_webfetch_definition(void);
const struct tool_device *tool_webfetch_device(void);
const struct tool_driver *tool_webfetch_driver(void);
