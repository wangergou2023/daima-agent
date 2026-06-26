/* 轻量委托任务状态表：为 delegate_task 提供后台任务句柄与轮询。 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "drivers/llm/llm_proxy.h"
#include "err.h"

#define DELEGATE_TASK_ID_LEN 16
#define DELEGATE_TASK_DESC_LEN 64
#define DELEGATE_TASK_AGENT_LEN 24
#define DELEGATE_TASK_OUTPUT_LEN 8192

typedef enum {
    DELEGATE_TASK_RUNNING = 0,
    DELEGATE_TASK_DONE,
    DELEGATE_TASK_FAILED,
} delegate_task_status_t;

typedef struct {
    char task_id[DELEGATE_TASK_ID_LEN];
    char subagent_type[DELEGATE_TASK_AGENT_LEN];
    char description[DELEGATE_TASK_DESC_LEN];
    char model[64];
    delegate_task_status_t status;
    err_t error;
    llm_async_chat_t *chat;
    char output[DELEGATE_TASK_OUTPUT_LEN];
} delegate_task_record_t;

err_t delegate_task_store_init(void);
err_t delegate_task_store_start(const char *task_id,
                                const char *subagent_type,
                                const char *description,
                                const char *model,
                                llm_async_chat_t *chat);
err_t delegate_task_store_poll(const char *task_id,
                               delegate_task_record_t *out);
err_t delegate_task_store_snapshot(const char *task_id,
                                   delegate_task_record_t *out);
void delegate_task_store_reset_for_test(void);
