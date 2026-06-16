/* 异步 turn 状态快照存储 */
#pragma once
#include "err.h"
#include "cjson.h"
#include <stdint.h>
#include <stdbool.h>

struct turn_snapshot {
    char chat_id[64];
    char channel[16];
    char source[16];
    cJSON *messages;        /* 对话历史 */
    char *system_prompt;    /* copy */
    int iteration;
    bool tool_budget_exhausted;
    uint64_t cancel_token;

    /* tool dispatch 跟踪 */
    char pending_task_id[16];  /* 当前等待的 task_id，空表示无等待 */
    uint64_t dispatched_at;
    uint32_t timeout_ms;
};

/* 按 chat_id 存取快照 */
void turn_context_save(const struct turn_snapshot *snap);
struct turn_snapshot *turn_context_load(const char *chat_id);
void turn_context_remove(const char *chat_id);

/* 按 task_id 找回 chat_id */
const char *turn_context_find_by_task(const char *task_id);

/* peek: 是否有回复到达（非阻塞） */
bool core_has_reply(void);