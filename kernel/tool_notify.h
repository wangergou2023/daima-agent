#pragma once

#include <stdbool.h>

#include "bus.h"
#include "err.h"

typedef struct {
    const char *tool_name;
    const char *tool_input;
    const char *target;
    const char *detail;
    const char *default_text;
    bool ok;
    long elapsed_ms;
} tool_activity_event_t;

err_t channel_runtime_send_tool_activity(const struct message *msg,
                                              const tool_activity_event_t *event);
