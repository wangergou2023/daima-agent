/* 工具执行异步恢复所需的 turn 快照仓库。
 * 这里只保存 resume 路径需要的持久快照，不负责当前同步回合的临时 I/O。
 */
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

    /* 父会话 interview / 交互恢复态 */
    char pending_request_type[32];
    char pending_request_id[64];
    char pending_request_prompt[512];

    /* 最近一次已被父链消费的 delegate resume 水位 */
    char consumed_delegate_coordinator_id[32];
    unsigned long consumed_delegate_visible_revision;

};

/* 按 chat_id 存取异步恢复快照 */
void turn_context_save(const struct turn_snapshot *snap);
bool turn_context_load_copy(const char *chat_id, struct turn_snapshot *out);
void turn_context_remove(const char *chat_id);
void turn_context_snapshot_cleanup(struct turn_snapshot *snap);

/* 按 task_id 找回 chat_id */
bool turn_context_find_by_task(const char *task_id, char *chat_id, size_t chat_id_size);
bool turn_context_set_pending_request(const char *chat_id,
                                      const char *request_type,
                                      const char *request_id,
                                      const char *prompt_text);
bool turn_context_clear_pending_request(const char *chat_id,
                                        const char *request_type,
                                        const char *request_id);
bool turn_context_set_delegate_resume_consumed(const char *chat_id,
                                               const char *coordinator_id,
                                               unsigned long visible_revision);

/* peek: 是否有回复到达（非阻塞） */
bool core_has_reply(void);
