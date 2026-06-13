#pragma once

#include <stdbool.h>

#include "core/bus.h"
#include "core/err.h"

typedef struct {
    const char *tool_name;
    const char *tool_input;
    const char *target;
    const char *detail;
    const char *default_text;
    bool ok;
    long elapsed_ms;
} daima_tool_activity_event_t;

daima_err_t channel_runtime_send_tool_activity(const daima_msg_t *msg,
                                              const daima_tool_activity_event_t *event);
