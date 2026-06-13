#pragma once

#include "err.h"

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#define SESSION_RECOVERY_MAX_MSG 2048

typedef struct {
    bool has_crash;
    char last_user_msg[SESSION_RECOVERY_MAX_MSG];
    char crash_reason[128];
    time_t crash_at;
    int turn_count;
} session_recovery_t;

session_recovery_t session_recovery_check(const char *chat_id);
daima_err_t session_recovery_save_crash(const char *chat_id,
                                         const char *last_user_msg,
                                         const char *crash_reason);
daima_err_t session_recovery_inject_prompt(const char *chat_id,
                                            char *system_prompt,
                                            size_t system_prompt_size);
void session_recovery_clear(const char *chat_id);
