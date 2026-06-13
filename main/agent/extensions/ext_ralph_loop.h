#pragma once

#include "agent/agent_turn_common.h"

#include <stdbool.h>

bool agent_extension_ralph_should_append_warning(daima_msg_t *msg, int iteration, char **io_final_text);
