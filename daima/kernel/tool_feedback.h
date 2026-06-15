#pragma once

#include "bus.h"
#include "err.h"

void agent_tool_feedback_send_activity(const struct message *msg,
                                       const char *tool_name,
                                       const char *tool_input,
                                       const char *tool_output,
                                       daima_err_t exec_err,
                                       long elapsed_ms);
