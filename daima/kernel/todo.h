#pragma once

#include "err.h"

#include <stdbool.h>
#include <stddef.h>

#define TODO_ENFORCER_MAX_STALE_TURNS 3

typedef struct {
    bool enabled;
    int max_stale_turns;
} todo_enforcer_cfg_t;

todo_enforcer_cfg_t todo_enforcer_load_cfg(void);
err_t todo_enforcer_record_progress(const char *chat_id, int todo_count, int completed_count);
err_t todo_enforcer_inject_prompt(const char *chat_id, char *system_prompt, size_t system_prompt_size);
err_t todo_enforcer_reset(const char *chat_id);
