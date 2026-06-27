#pragma once

#include "delegate_task_store.h"

#include "cjson.h"

#define DELEGATE_CHILD_SESSION_HISTORY_LIMIT_DEFAULT 80
#define DELEGATE_CHILD_SESSION_HISTORY_BUF_SIZE 65536

typedef struct {
    int history_limit;
    unsigned long history_after_seq;
    unsigned long frame_after_seq;
    unsigned long commit_after_seq;
} delegate_child_session_json_options_t;

cJSON *delegate_child_session_json_build_from_task(const delegate_task_record_t *task_snapshot,
                                                   const delegate_child_session_json_options_t *options);
