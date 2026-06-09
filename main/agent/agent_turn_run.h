#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "bus/message_bus.h"
#include "cJSON.h"
#include "daima_err.h"

daima_err_t agent_turn_run(
    const char *system_prompt,
    cJSON *messages,
    const char *tools_json,
    const daima_msg_t *msg,
    uint64_t cancel_token,
    char **out_final_text,
    int *out_iteration,
    bool *out_tool_budget_exhausted,
    bool *out_cancelled);
