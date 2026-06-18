#pragma once

#include "err.h"

#include <stdbool.h>

#define RALPH_LOOP_MAX_ITERATIONS 10
#define RALPH_LOOP_IDLE_TIMEOUT_MS 300000  // 5分钟无进展则停止

typedef struct {
    bool enabled;
    int max_iterations;
    int idle_timeout_ms;
} ralph_loop_cfg_t;

ralph_loop_cfg_t ralph_loop_load_cfg(void);
bool ralph_loop_should_continue(const char *chat_id,
                                int iteration,
                                const char *final_text);
void ralph_loop_reset(const char *chat_id);
bool ralph_loop_append_warning_if_needed(const char *chat_id, int iteration,
                                          char **io_final_text);
