#pragma once

#include <stdbool.h>

#include "ipc/bus.h"

typedef enum tool_decomposition_mode {
    TOOL_DECOMP_NONE = 0,
    TOOL_DECOMP_SERIAL,
    TOOL_DECOMP_PARALLEL,
} tool_decomposition_mode_t;

const char *tool_decomposition_mode_name(tool_decomposition_mode_t mode);
tool_decomposition_mode_t tool_decomposition_policy_classify_message(const struct message *msg);
bool tool_decomposition_policy_requires_delegate_only(const struct message *msg);
bool tool_decomposition_policy_prefers_parallel(const struct message *msg);
