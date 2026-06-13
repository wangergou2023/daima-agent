#pragma once

#include <stdbool.h>

#include "bus.h"
#include "err.h"

void agent_turn_finish(
    daima_msg_t *msg,
    char **io_final_text,
    char **io_reasoning_text,
    daima_err_t turn_err,
    int iteration,
    bool tool_budget_exhausted,
    bool cancelled);
