/* tool_bus 视图：统一读取已绑定工具的元信息。 */
#pragma once

#include "drivers/tool/tool_types.h"
#include "err.h"
#include <stddef.h>
#include <stdbool.h>

const struct tool_device *tool_bus_get_device(const char *name);
bool tool_bus_channel_allows_tool(const char *channel, const char *tool_name);
err_t tool_bus_execute(const char *name, const char *input_json, char *output, size_t output_size);
err_t tool_bus_execute_for_channel(const char *channel,
                                   const char *name,
                                   const char *input_json,
                                   char *output,
                                   size_t output_size);
const char *tool_bus_tools_json(void);
const char *tool_bus_tools_json_for_channel(const char *channel);
const char *tool_bus_tools_json_for_channel_without_delegate(const char *channel);
const char *tool_bus_tools_json_delegate_only(void);
