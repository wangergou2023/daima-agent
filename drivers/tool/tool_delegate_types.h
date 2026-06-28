/* delegate_task shared request/types */
#pragma once

#include <stdbool.h>

#include "delegate/delegate_task_store.h"

typedef enum delegate_subagent_kind {
    DELEGATE_SUBAGENT_EXPLORE = 0,
    DELEGATE_SUBAGENT_LIBRARIAN,
    DELEGATE_SUBAGENT_ORACLE,
    DELEGATE_SUBAGENT_IMPLEMENT,
    DELEGATE_SUBAGENT_INVALID,
} delegate_subagent_kind_t;

typedef struct delegate_request {
    struct {
        char tool_name[32];
        char input_json[1024];
        bool continue_on_error;
    } preflight_tool;
    char action[16];
    char scope[16];
    char target_path[512];
    char description[64];
    char prompt[2048];
    char subagent_type[24];
    char depends_on[DELEGATE_TASK_DEPENDS_ON_LEN];
    char team_name[DELEGATE_TEAM_NAME_LEN];
    char dispatch_mode[24];
    char task_id[DELEGATE_TASK_ID_LEN];
    char task_key[DELEGATE_TASK_KEY_LEN];
    char coordinator_id[DELEGATE_COORDINATOR_ID_LEN];
    bool run_in_background;
    bool is_batch;
    int batch_count;
    struct {
        char task_key[DELEGATE_TASK_KEY_LEN];
        char description[64];
        char prompt[2048];
        char subagent_type[24];
        char target_path[512];
        char depends_on[DELEGATE_TASK_DEPENDS_ON_LEN];
        struct {
            char tool_name[32];
            char input_json[1024];
            bool continue_on_error;
        } preflight_tool;
    } batch_tasks[DELEGATE_COORDINATOR_AGENTS_MAX];
} delegate_request_t;
