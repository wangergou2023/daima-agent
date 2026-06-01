#pragma once

#include <stdbool.h>

#include "bus/message_bus.h"
#include "cJSON.h"
#include "daima_err.h"

daima_err_t agent_turn_run(
    const char *system_prompt,
    cJSON *messages,
    const char *tools_json,
    const daima_msg_t *msg,
    char **out_final_text,
    int *out_iteration,
    bool *out_tool_budget_exhausted);
