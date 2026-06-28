#pragma once

#include "err.h"

err_t tool_delegate_launch_ready_background_subagents(const char *coordinator_id,
                                                      const char *parent_chat_id);
err_t tool_delegate_launch_one_ready_background_subagent(const char *coordinator_id,
                                                         const char *parent_chat_id);
