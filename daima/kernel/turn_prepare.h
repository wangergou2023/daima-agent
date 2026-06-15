#pragma once

#include <stddef.h>

#include "plan.h"
#include "bus.h"
#include "cJSON.h"
#include "err.h"

daima_err_t agent_turn_prepare(
    const struct message *msg,
    const struct plan *plan,
    char *system_prompt,
    size_t system_prompt_size,
    char *history_json,
    size_t history_json_size,
    cJSON **out_messages);
