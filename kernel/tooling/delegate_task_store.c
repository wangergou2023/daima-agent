/* 轻量委托任务状态表：后台轮询异步 LLM，并缓存最终结果。 */
#include "delegate_task_store.h"

#include <string.h>

#include "linux/kernel.h"
#include "linux/mutex.h"
#include "linux/printk.h"

#define DELEGATE_TASK_STORE_MAX 8

static struct mutex s_delegate_mutex;
static bool s_delegate_mutex_inited = false;
static delegate_task_record_t s_records[DELEGATE_TASK_STORE_MAX];

static void ensure_store_init(void)
{
    if (!s_delegate_mutex_inited) {
        mutex_init(&s_delegate_mutex);
        s_delegate_mutex_inited = true;
    }
}

static int find_record_index(const char *task_id)
{
    if (!task_id || !task_id[0]) {
        return -1;
    }
    for (int i = 0; i < DELEGATE_TASK_STORE_MAX; i++) {
        if (s_records[i].task_id[0] && strcmp(s_records[i].task_id, task_id) == 0) {
            return i;
        }
    }
    return -1;
}

static int allocate_record_index(void)
{
    for (int i = 0; i < DELEGATE_TASK_STORE_MAX; i++) {
        if (!s_records[i].task_id[0]) {
            return i;
        }
    }
    return 0;
}

static void clear_record(delegate_task_record_t *record)
{
    if (!record) return;
    if (record->chat) {
        llm_chat_async_free(record->chat);
    }
    memset(record, 0, sizeof(*record));
}

static void fill_output_from_response(delegate_task_record_t *record, llm_response_t *resp)
{
    if (!record || !resp) return;
    if (resp->text && resp->text[0]) {
        strscpy(record->output, resp->text, sizeof(record->output));
    } else if (resp->reasoning_content && resp->reasoning_content[0]) {
        strscpy(record->output, resp->reasoning_content, sizeof(record->output));
    } else {
        strscpy(record->output, "(empty subagent response)", sizeof(record->output));
    }
}

err_t delegate_task_store_init(void)
{
    ensure_store_init();
    return 0;
}

err_t delegate_task_store_start(const char *task_id,
                                const char *subagent_type,
                                const char *description,
                                const char *model,
                                llm_async_chat_t *chat)
{
    if (!task_id || !task_id[0] || !chat) {
        return ERR_INVALID_ARG;
    }

    ensure_store_init();
    mutex_lock(&s_delegate_mutex);

    int idx = find_record_index(task_id);
    if (idx < 0) {
        idx = allocate_record_index();
    } else {
        clear_record(&s_records[idx]);
    }

    delegate_task_record_t *record = &s_records[idx];
    memset(record, 0, sizeof(*record));
    strscpy(record->task_id, task_id, sizeof(record->task_id));
    strscpy(record->subagent_type, subagent_type ? subagent_type : "", sizeof(record->subagent_type));
    strscpy(record->description, description ? description : "", sizeof(record->description));
    strscpy(record->model, model ? model : "", sizeof(record->model));
    record->status = DELEGATE_TASK_RUNNING;
    record->error = 0;
    record->chat = chat;

    mutex_unlock(&s_delegate_mutex);
    pr_info("delegate_task_store: started task_id=%s subagent=%s model=%s",
            record->task_id, record->subagent_type, record->model);
    return 0;
}

err_t delegate_task_store_snapshot(const char *task_id,
                                   delegate_task_record_t *out)
{
    if (!out) {
        return ERR_INVALID_ARG;
    }

    ensure_store_init();
    mutex_lock(&s_delegate_mutex);
    int idx = find_record_index(task_id);
    if (idx < 0) {
        mutex_unlock(&s_delegate_mutex);
        return ERR_NOT_FOUND;
    }
    *out = s_records[idx];
    mutex_unlock(&s_delegate_mutex);
    return 0;
}

err_t delegate_task_store_poll(const char *task_id,
                               delegate_task_record_t *out)
{
    if (!task_id || !task_id[0] || !out) {
        return ERR_INVALID_ARG;
    }

    ensure_store_init();
    mutex_lock(&s_delegate_mutex);
    int idx = find_record_index(task_id);
    if (idx < 0) {
        mutex_unlock(&s_delegate_mutex);
        return ERR_NOT_FOUND;
    }

    delegate_task_record_t *record = &s_records[idx];
    if (record->status == DELEGATE_TASK_RUNNING && record->chat && llm_chat_async_is_done(record->chat)) {
        llm_response_t resp;
        memset(&resp, 0, sizeof(resp));
        err_t err = llm_chat_async_get_response(record->chat, &resp);
        if (err == 0) {
            fill_output_from_response(record, &resp);
            record->status = DELEGATE_TASK_DONE;
        } else {
            record->status = DELEGATE_TASK_FAILED;
            record->error = err;
            strscpy(record->output, err_name(err), sizeof(record->output));
        }
        llm_response_free(&resp);
        llm_chat_async_free(record->chat);
        record->chat = NULL;
        pr_info("delegate_task_store: completed task_id=%s status=%d err=%d",
                record->task_id, record->status, record->error);
    }

    *out = *record;
    mutex_unlock(&s_delegate_mutex);
    return 0;
}

void delegate_task_store_reset_for_test(void)
{
    ensure_store_init();
    mutex_lock(&s_delegate_mutex);
    for (int i = 0; i < DELEGATE_TASK_STORE_MAX; i++) {
        clear_record(&s_records[i]);
    }
    mutex_unlock(&s_delegate_mutex);
}
