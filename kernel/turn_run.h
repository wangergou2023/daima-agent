#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "bus.h"
#include "cJSON.h"
#include "err.h"

err_t agent_turn_run(
    const char *system_prompt,
    cJSON *messages,
    const char *tools_json,
    const struct message *msg,
    const char *model_override,
    uint64_t cancel_token,
    char **out_final_text,
    char **out_reasoning_text,
    int *out_iteration,
    bool *out_tool_budget_exhausted,
    bool *out_cancelled);
