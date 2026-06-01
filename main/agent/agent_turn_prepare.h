#pragma once

#include <stddef.h>

#include "bus/message_bus.h"
#include "cJSON.h"
#include "daima_err.h"

daima_err_t agent_turn_prepare(
    const daima_msg_t *msg,
    char *system_prompt,
    size_t system_prompt_size,
    char *history_json,
    size_t history_json_size,
    cJSON **out_messages);
