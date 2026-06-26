/* 轻量委托任务状态表：后台轮询异步 LLM，并缓存最终结果。 */
#include "delegate_task_store.h"

#include <string.h>

#include "linux/kernel.h"
#include "linux/mutex.h"
#include "linux/printk.h"
#include "drivers/tool/tool_delegate.h"

#include <time.h>

#define DELEGATE_TASK_STORE_MAX 8
#define DELEGATE_COORDINATOR_STORE_MAX 4

static struct mutex s_delegate_mutex;
static bool s_delegate_mutex_inited = false;
static delegate_task_record_t s_records[DELEGATE_TASK_STORE_MAX];
static delegate_coordinator_record_t s_coordinators[DELEGATE_COORDINATOR_STORE_MAX];

static long monotonic_ms_now(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (long)(ts.tv_sec * 1000L + ts.tv_nsec / 1000000L);
}

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

static int find_coordinator_index(const char *coordinator_id)
{
    if (!coordinator_id || !coordinator_id[0]) {
        return -1;
    }
    for (int i = 0; i < DELEGATE_COORDINATOR_STORE_MAX; i++) {
        if (s_coordinators[i].coordinator_id[0] &&
            strcmp(s_coordinators[i].coordinator_id, coordinator_id) == 0) {
            return i;
        }
    }
    return -1;
}

static int allocate_coordinator_index(void)
{
    for (int i = 0; i < DELEGATE_COORDINATOR_STORE_MAX; i++) {
        if (!s_coordinators[i].coordinator_id[0]) {
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
    char final_clean[DELEGATE_TASK_OUTPUT_LEN];
    char reasoning_clean[DELEGATE_TASK_OUTPUT_LEN];
    char final_json_summary[DELEGATE_TASK_OUTPUT_LEN];
    char reasoning_json_summary[DELEGATE_TASK_OUTPUT_LEN];

    if (!record || !resp) return;
    tool_delegate_sanitize_summary_text_copy(final_clean, sizeof(final_clean), resp->text);
    tool_delegate_sanitize_summary_text_copy(reasoning_clean, sizeof(reasoning_clean), resp->reasoning_content);
    if (tool_delegate_parse_result_json_rendered(final_clean, final_json_summary, sizeof(final_json_summary))) {
        strscpy(record->output, final_json_summary, sizeof(record->output));
        pr_info("delegate_task_store result: task_id=%s source=final_json rendered=1 output_len=%zu",
                record->task_id,
                strlen(record->output));
    } else if (tool_delegate_parse_result_json_rendered(reasoning_clean, reasoning_json_summary, sizeof(reasoning_json_summary))) {
        strscpy(record->output, reasoning_json_summary, sizeof(record->output));
        pr_info("delegate_task_store result: task_id=%s source=reasoning_json rendered=1 output_len=%zu",
                record->task_id,
                strlen(record->output));
    } else if (tool_delegate_finalize_result_json(record->subagent_type,
                                                  record->description,
                                                  final_clean[0] ? final_clean : reasoning_clean,
                                                  final_json_summary,
                                                  sizeof(final_json_summary))) {
        strscpy(record->output, final_json_summary, sizeof(record->output));
        pr_info("delegate_task_store result: task_id=%s source=finalizer rendered=1 output_len=%zu",
                record->task_id,
                strlen(record->output));
    } else {
        strscpy(record->output,
                tool_delegate_safe_output_text(final_clean,
                                               reasoning_clean,
                                               false,
                                               false),
                sizeof(record->output));
        pr_info("delegate_task_store result: task_id=%s source=safe_fallback rendered=0 output_len=%zu",
                record->task_id,
                strlen(record->output));
    }
}

static void fill_target_files_from_output(delegate_task_record_t *record)
{
    if (!record) {
        return;
    }
    record->target_files[0] = '\0';
    record->write_approved = false;

    if (strcmp(record->subagent_type, "implement") != 0) {
        return;
    }

    const char *marker = strstr(record->output, "target_files:");
    if (!marker) {
        return;
    }
    marker += strlen("target_files:");
    while (*marker == ' ' || *marker == '\n' || *marker == '\t') {
        marker++;
    }
    size_t i = 0;
    while (marker[i] && marker[i] != '\n' && i < sizeof(record->target_files) - 1) {
        record->target_files[i] = marker[i];
        i++;
    }
    record->target_files[i] = '\0';
}

static bool filesets_overlap(const char *left, const char *right)
{
    if (!left || !right || !left[0] || !right[0]) {
        return false;
    }
    return strstr(left, right) != NULL || strstr(right, left) != NULL;
}

static void refresh_implement_write_approvals_locked(delegate_coordinator_record_t *coordinator)
{
    if (!coordinator) {
        return;
    }

    for (int i = 0; i < coordinator->agent_count; i++) {
        int task_idx = find_record_index(coordinator->agents[i].task_id);
        if (task_idx < 0) {
            continue;
        }
        delegate_task_record_t *record = &s_records[task_idx];
        if (strcmp(record->subagent_type, "implement") != 0) {
            record->write_approved = false;
            continue;
        }
        record->write_approved = (record->status == DELEGATE_TASK_DONE && record->target_files[0]);
    }

    for (int i = 0; i < coordinator->agent_count; i++) {
        int left_idx = find_record_index(coordinator->agents[i].task_id);
        if (left_idx < 0) {
            continue;
        }
        delegate_task_record_t *left = &s_records[left_idx];
        if (!left->write_approved) {
            continue;
        }
        for (int j = i + 1; j < coordinator->agent_count; j++) {
            int right_idx = find_record_index(coordinator->agents[j].task_id);
            if (right_idx < 0) {
                continue;
            }
            delegate_task_record_t *right = &s_records[right_idx];
            if (!right->write_approved) {
                continue;
            }
            if (filesets_overlap(left->target_files, right->target_files)) {
                right->write_approved = false;
            }
        }
    }
}

static void poll_record_locked(delegate_task_record_t *record)
{
    if (!record) {
        return;
    }

    if (record->status == DELEGATE_TASK_RUNNING && record->chat && llm_chat_async_is_done(record->chat)) {
        llm_response_t resp;
        memset(&resp, 0, sizeof(resp));
        err_t err = llm_chat_async_get_response(record->chat, &resp);
        if (err == 0) {
            fill_output_from_response(record, &resp);
            fill_target_files_from_output(record);
            record->status = DELEGATE_TASK_DONE;
            record->finished_ms = monotonic_ms_now();
        } else {
            record->status = DELEGATE_TASK_FAILED;
            record->error = err;
            record->finished_ms = monotonic_ms_now();
            strscpy(record->output, err_name(err), sizeof(record->output));
        }
        llm_response_free(&resp);
        llm_chat_async_free(record->chat);
        record->chat = NULL;
        pr_info("delegate_task_store: completed task_id=%s status=%d err=%d",
                record->task_id, record->status, record->error);
    }
}

static void refresh_coordinator_locked(delegate_coordinator_record_t *coordinator)
{
    if (!coordinator || !coordinator->coordinator_id[0]) {
        return;
    }

    int agent_count = coordinator->agent_count;
    int completed_count = 0;
    bool any_running = false;
    bool any_failed = false;

    refresh_implement_write_approvals_locked(coordinator);

    for (int i = 0; i < agent_count; i++) {
        delegate_coordinator_agent_view_t *view = &coordinator->agents[i];
        int task_idx = find_record_index(view->task_id);
        if (task_idx < 0) {
            continue;
        }
        delegate_task_record_t *record = &s_records[task_idx];
        strscpy(view->subagent_type, record->subagent_type, sizeof(view->subagent_type));
        strscpy(view->description, record->description, sizeof(view->description));
        strscpy(view->model, record->model, sizeof(view->model));
        strscpy(view->target_files, record->target_files, sizeof(view->target_files));
        view->write_approved = record->write_approved;
        strscpy(view->output, record->output, sizeof(view->output));
        if (record->started_ms > 0) {
            long end_ms = record->finished_ms > 0 ? record->finished_ms : monotonic_ms_now();
            view->elapsed_ms = end_ms > record->started_ms ? (end_ms - record->started_ms) : 0;
        } else {
            view->elapsed_ms = 0;
        }

        if (record->status == DELEGATE_TASK_DONE) {
            strscpy(view->status, "done", sizeof(view->status));
            completed_count++;
        } else if (record->status == DELEGATE_TASK_FAILED) {
            strscpy(view->status, "error", sizeof(view->status));
            completed_count++;
            any_failed = true;
        } else {
            strscpy(view->status, "running", sizeof(view->status));
            any_running = true;
        }
    }

    coordinator->completed_count = completed_count;
    if (any_running) {
        strscpy(coordinator->status, "running", sizeof(coordinator->status));
    } else if (any_failed) {
        strscpy(coordinator->status, "failed", sizeof(coordinator->status));
    } else {
        strscpy(coordinator->status, "done", sizeof(coordinator->status));
    }
    coordinator->changed = true;
}

err_t delegate_task_store_init(void)
{
    ensure_store_init();
    return 0;
}

err_t delegate_task_store_start(const char *task_id,
                                const char *coordinator_id,
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
    strscpy(record->coordinator_id, coordinator_id ? coordinator_id : "", sizeof(record->coordinator_id));
    strscpy(record->subagent_type, subagent_type ? subagent_type : "", sizeof(record->subagent_type));
    strscpy(record->description, description ? description : "", sizeof(record->description));
    strscpy(record->model, model ? model : "", sizeof(record->model));
    record->status = DELEGATE_TASK_RUNNING;
    record->error = 0;
    record->chat = chat;
    record->started_ms = monotonic_ms_now();
    record->finished_ms = 0;

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
    poll_record_locked(record);
    if (record->coordinator_id[0]) {
        int coord_idx = find_coordinator_index(record->coordinator_id);
        if (coord_idx >= 0) {
            refresh_coordinator_locked(&s_coordinators[coord_idx]);
        }
    }

    *out = *record;
    mutex_unlock(&s_delegate_mutex);
    return 0;
}

err_t delegate_task_store_start_coordinator(const char *coordinator_id,
                                            const char *chat_id)
{
    if (!coordinator_id || !coordinator_id[0]) {
        return ERR_INVALID_ARG;
    }

    ensure_store_init();
    mutex_lock(&s_delegate_mutex);
    int idx = find_coordinator_index(coordinator_id);
    if (idx < 0) {
        idx = allocate_coordinator_index();
    }
    memset(&s_coordinators[idx], 0, sizeof(s_coordinators[idx]));
    strscpy(s_coordinators[idx].coordinator_id, coordinator_id, sizeof(s_coordinators[idx].coordinator_id));
    strscpy(s_coordinators[idx].chat_id, chat_id ? chat_id : "", sizeof(s_coordinators[idx].chat_id));
    strscpy(s_coordinators[idx].status, "running", sizeof(s_coordinators[idx].status));
    s_coordinators[idx].changed = true;
    s_coordinators[idx].completion_notified = false;
    mutex_unlock(&s_delegate_mutex);
    return 0;
}

err_t delegate_task_store_attach_task(const char *coordinator_id,
                                      const char *task_id)
{
    if (!coordinator_id || !coordinator_id[0] || !task_id || !task_id[0]) {
        return ERR_INVALID_ARG;
    }

    ensure_store_init();
    mutex_lock(&s_delegate_mutex);
    int coord_idx = find_coordinator_index(coordinator_id);
    int task_idx = find_record_index(task_id);
    if (coord_idx < 0 || task_idx < 0) {
        mutex_unlock(&s_delegate_mutex);
        return ERR_NOT_FOUND;
    }

    delegate_coordinator_record_t *coordinator = &s_coordinators[coord_idx];
    if (coordinator->agent_count >= DELEGATE_COORDINATOR_AGENTS_MAX) {
        mutex_unlock(&s_delegate_mutex);
        return ERR_NO_MEM;
    }

    delegate_coordinator_agent_view_t *view = &coordinator->agents[coordinator->agent_count++];
    memset(view, 0, sizeof(*view));
    strscpy(view->task_id, task_id, sizeof(view->task_id));
    strscpy(view->description, s_records[task_idx].description, sizeof(view->description));
    strscpy(view->subagent_type, s_records[task_idx].subagent_type, sizeof(view->subagent_type));
    strscpy(view->model, s_records[task_idx].model, sizeof(view->model));
    strscpy(view->status, "running", sizeof(view->status));
    coordinator->changed = true;
    mutex_unlock(&s_delegate_mutex);
    return 0;
}

err_t delegate_task_store_poll_coordinator(const char *coordinator_id,
                                           delegate_coordinator_record_t *out)
{
    if (!coordinator_id || !coordinator_id[0] || !out) {
        return ERR_INVALID_ARG;
    }

    ensure_store_init();
    mutex_lock(&s_delegate_mutex);
    int coord_idx = find_coordinator_index(coordinator_id);
    if (coord_idx < 0) {
        mutex_unlock(&s_delegate_mutex);
        return ERR_NOT_FOUND;
    }

    delegate_coordinator_record_t *coordinator = &s_coordinators[coord_idx];
    for (int i = 0; i < coordinator->agent_count; i++) {
        int task_idx = find_record_index(coordinator->agents[i].task_id);
        if (task_idx >= 0) {
            poll_record_locked(&s_records[task_idx]);
        }
    }
    refresh_coordinator_locked(coordinator);
    *out = *coordinator;
    coordinator->changed = false;
    mutex_unlock(&s_delegate_mutex);
    return 0;
}

err_t delegate_task_store_mark_completion_notified(const char *coordinator_id)
{
    if (!coordinator_id || !coordinator_id[0]) {
        return ERR_INVALID_ARG;
    }

    ensure_store_init();
    mutex_lock(&s_delegate_mutex);
    int coord_idx = find_coordinator_index(coordinator_id);
    if (coord_idx < 0) {
        mutex_unlock(&s_delegate_mutex);
        return ERR_NOT_FOUND;
    }
    s_coordinators[coord_idx].completion_notified = true;
    mutex_unlock(&s_delegate_mutex);
    return 0;
}

bool delegate_task_store_poll_updates(delegate_coordinator_record_t *out,
                                      size_t max_out)
{
    bool found = false;
    if (!out || max_out == 0) {
        return false;
    }

    ensure_store_init();
    mutex_lock(&s_delegate_mutex);
    size_t emitted = 0;
    for (int i = 0; i < DELEGATE_COORDINATOR_STORE_MAX && emitted < max_out; i++) {
        delegate_coordinator_record_t *coordinator = &s_coordinators[i];
        if (!coordinator->coordinator_id[0]) {
            continue;
        }
        for (int j = 0; j < coordinator->agent_count; j++) {
            int task_idx = find_record_index(coordinator->agents[j].task_id);
            if (task_idx >= 0) {
                poll_record_locked(&s_records[task_idx]);
            }
        }
        refresh_coordinator_locked(coordinator);
        if (!coordinator->changed) {
            continue;
        }
        out[emitted++] = *coordinator;
        coordinator->changed = false;
        found = true;
    }
    mutex_unlock(&s_delegate_mutex);
    return found;
}

void delegate_task_store_reset_for_test(void)
{
    ensure_store_init();
    mutex_lock(&s_delegate_mutex);
    for (int i = 0; i < DELEGATE_TASK_STORE_MAX; i++) {
        clear_record(&s_records[i]);
    }
    memset(s_coordinators, 0, sizeof(s_coordinators));
    mutex_unlock(&s_delegate_mutex);
}
