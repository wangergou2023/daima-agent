#pragma once

#include "err.h"

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#define COMPACTION_RECOVERY_MAX_TODOS    4096
#define COMPACTION_RECOVERY_MAX_MESSAGE  1024
#define COMPACTION_RECOVERY_MAX_TASK     512

typedef struct {
    char active_todos[COMPACTION_RECOVERY_MAX_TODOS];
    char last_user_message[COMPACTION_RECOVERY_MAX_MESSAGE];
    char current_task[COMPACTION_RECOVERY_MAX_TASK];
    time_t snapshot_at;
    bool is_valid;
} compaction_recovery_t;

err_t compaction_recovery_snapshot(const char *chat_id);
err_t compaction_recovery_inject(const char *chat_id, char *system_prompt, size_t system_prompt_size);
err_t compaction_recovery_clear(const char *chat_id);
