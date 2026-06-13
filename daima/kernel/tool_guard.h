#pragma once

#include <stdbool.h>

#include "err.h"

bool agent_tool_protocol_failure_should_stop(const char *tool_name,
                                             const char *tool_input,
                                             const char *tool_output,
                                             daima_err_t tool_err);

