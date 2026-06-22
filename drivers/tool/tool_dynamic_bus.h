/* 动态工具的 tool_bus 注册生命周期。 */

#pragma once

#include "drivers/tool/tool_types.h"

#define TOOL_DYNAMIC_BUS_MAX 32

err_t tool_dynamic_bus_register(const struct tool *tool);
err_t tool_dynamic_bus_unregister(const char *tool_name);
