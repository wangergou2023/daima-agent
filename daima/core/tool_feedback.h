#pragma once

#include "core/bus.h"
#include "core/err.h"

void agent_tool_feedback_send_activity(const daima_msg_t *msg,
                                       const char *tool_name,
                                       const char *tool_input,
                                       const char *tool_output,
                                       daima_err_t exec_err,
                                       long elapsed_ms);
