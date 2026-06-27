/* 轻量委托任务状态表：后台轮询异步 LLM，并缓存最终结果。 */
#include "delegate_task_store_internal.h"

#include <string.h>

#include "linux/kernel.h"
#include "linux/mutex.h"
#include "linux/printk.h"
#include "drivers/memory/session_store.h"
#include "drivers/tool/tool_delegate.h"

#include <time.h>

struct mutex s_delegate_mutex;
bool s_delegate_mutex_inited = false;
delegate_task_record_t s_records[DELEGATE_TASK_STORE_MAX];
delegate_coordinator_record_t s_coordinators[DELEGATE_COORDINATOR_STORE_MAX];
unsigned long s_visible_revision_seq = 1;

long monotonic_ms_now(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (long)(ts.tv_sec * 1000L + ts.tv_nsec / 1000000L);
}

void ensure_store_init(void)
{
    if (!s_delegate_mutex_inited) {
        mutex_init(&s_delegate_mutex);
        s_delegate_mutex_inited = true;
    }
}

int find_record_index(const char *task_id)
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

int find_record_index_by_session(const char *session_id)
{
    if (!session_id || !session_id[0]) {
        return -1;
    }
    for (int i = 0; i < DELEGATE_TASK_STORE_MAX; i++) {
        if (s_records[i].session_id[0] && strcmp(s_records[i].session_id, session_id) == 0) {
            return i;
        }
    }
    return -1;
}

int allocate_record_index(void)
{
    for (int i = 0; i < DELEGATE_TASK_STORE_MAX; i++) {
        if (!s_records[i].task_id[0]) {
            return i;
        }
    }
    return 0;
}

int find_coordinator_index(const char *coordinator_id)
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

int allocate_coordinator_index(void)
{
    for (int i = 0; i < DELEGATE_COORDINATOR_STORE_MAX; i++) {
        if (!s_coordinators[i].coordinator_id[0]) {
            return i;
        }
    }
    return 0;
}

void clear_record(delegate_task_record_t *record)
{
    if (!record) return;
    memset(record, 0, sizeof(*record));
}

const char *task_status_name(delegate_task_status_t status)
{
    switch (status) {
    case DELEGATE_TASK_QUEUED:
        return "queued";
    case DELEGATE_TASK_RUNNING:
        return "running";
    case DELEGATE_TASK_DONE:
        return "done";
    case DELEGATE_TASK_FAILED:
        return "error";
    default:
        return "running";
    }
}

bool filesets_overlap(const char *left, const char *right)
{
    if (!left || !right || !left[0] || !right[0]) {
        return false;
    }
    return strstr(left, right) != NULL || strstr(right, left) != NULL;
}

bool text_has_effective_output(const char *text)
{
    if (!text) {
        return false;
    }

    while (*text == ' ' || *text == '\n' || *text == '\r' || *text == '\t') {
        text++;
    }

    if (!*text) {
        return false;
    }

    if (strcmp(text, "sudo password was not provided") == 0) {
        return false;
    }
    if (strcmp(text, "Subagent stopped before producing findings.") == 0) {
        return false;
    }
    if (strcmp(text, "delegate protocol failure") == 0) {
        return false;
    }
    if (strstr(text, "\"error\":\"sudo_password_cancelled\"") != NULL) {
        return false;
    }
    if (strstr(text, "\"status\":\"sudo_password_required\"") != NULL) {
        return false;
    }

    return true;
}

static bool record_has_terminal_blocker(const delegate_task_record_t *record)
{
    if (!record) {
        return false;
    }

    return record->blocker_kind[0] != '\0' || record->blocker_text[0] != '\0';
}

err_t delegate_task_store_init(void)
{
    ensure_store_init();
    return 0;
}

static err_t store_task_record_locked(const char *task_id,
                                      const char *coordinator_id,
                                      const char *session_id,
                                      const char *subagent_type,
                                      const char *task_key,
                                      const char *description,
                                      const char *prompt,
                                      const char *model,
                                      const char *scope_path,
                                      const char *scope_kind,
                                      const char *analysis_focus,
                                      const char *depends_on,
                                      const delegate_preflight_tool_view_t *preflight_tool,
                                      delegate_task_status_t initial_status)
{
    if (!task_id || !task_id[0]) {
        return ERR_INVALID_ARG;
    }

    if (session_id && session_id[0]) {
        session_store_clear(session_id);
    }

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
    strscpy(record->session_id, session_id ? session_id : "", sizeof(record->session_id));
    strscpy(record->subagent_type, subagent_type ? subagent_type : "", sizeof(record->subagent_type));
    strscpy(record->task_key, task_key ? task_key : "", sizeof(record->task_key));
    strscpy(record->description, description ? description : "", sizeof(record->description));
    strscpy(record->prompt, prompt ? prompt : "", sizeof(record->prompt));
    strscpy(record->model, model ? model : "", sizeof(record->model));
    strscpy(record->scope_path, scope_path ? scope_path : "", sizeof(record->scope_path));
    strscpy(record->scope_kind, scope_kind ? scope_kind : "", sizeof(record->scope_kind));
    strscpy(record->analysis_focus, analysis_focus ? analysis_focus : "", sizeof(record->analysis_focus));
    strscpy(record->depends_on, depends_on ? depends_on : "", sizeof(record->depends_on));
    if (preflight_tool) {
        record->preflight_tool = *preflight_tool;
    }
    record->status = initial_status;
    record->error = 0;
    record->started_ms = initial_status == DELEGATE_TASK_RUNNING ? monotonic_ms_now() : 0;
    record->finished_ms = 0;
    record->blocker_kind[0] = '\0';
    record->blocker_text[0] = '\0';
    memset(&record->pending_request, 0, sizeof(record->pending_request));
    memset(&record->child_session, 0, sizeof(record->child_session));
    if (initial_status == DELEGATE_TASK_RUNNING) {
        session_record_start(record);
    } else {
        session_record_queued(record);
    }
    return 0;
}

err_t delegate_task_store_plan(const char *task_id,
                               const char *coordinator_id,
                               const char *session_id,
                               const char *subagent_type,
                               const char *task_key,
                               const char *description,
                               const char *prompt,
                               const char *model,
                               const char *scope_path,
                               const char *scope_kind,
                               const char *analysis_focus,
                               const char *depends_on,
                               const delegate_preflight_tool_view_t *preflight_tool)
{
    ensure_store_init();
    mutex_lock(&s_delegate_mutex);
    err_t err = store_task_record_locked(task_id, coordinator_id, session_id, subagent_type, task_key,
                                         description, prompt, model, scope_path, scope_kind, analysis_focus,
                                         depends_on, preflight_tool, DELEGATE_TASK_QUEUED);
    if (err == 0 && coordinator_id && coordinator_id[0]) {
        int coord_idx = find_coordinator_index(coordinator_id);
        if (coord_idx >= 0) {
            refresh_coordinator_locked(&s_coordinators[coord_idx]);
            bump_coordinator_visible_revision_locked(&s_coordinators[coord_idx]);
        }
    }
    mutex_unlock(&s_delegate_mutex);
    return err;
}

err_t delegate_task_store_start(const char *task_id,
                                const char *coordinator_id,
                                const char *session_id,
                                const char *subagent_type,
                                const char *task_key,
                                const char *description,
                                const char *prompt,
                                const char *model,
                                const char *scope_path,
                                const char *scope_kind,
                                const char *analysis_focus,
                                const delegate_preflight_tool_view_t *preflight_tool)
{
    if (!task_id || !task_id[0]) {
        return ERR_INVALID_ARG;
    }

    ensure_store_init();
    mutex_lock(&s_delegate_mutex);
    err_t err = store_task_record_locked(task_id, coordinator_id, session_id, subagent_type, task_key,
                                         description, prompt, model, scope_path, scope_kind, analysis_focus,
                                         "", preflight_tool, DELEGATE_TASK_RUNNING);
    mutex_unlock(&s_delegate_mutex);
    if (err != 0) {
        return err;
    }
    pr_info("delegate_task_store: started task_id=%s subagent=%s model=%s",
            task_id, subagent_type ? subagent_type : "", model ? model : "");
    return 0;
}

err_t delegate_task_store_mark_running(const char *task_id)
{
    if (!task_id || !task_id[0]) {
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
    if (record->status != DELEGATE_TASK_RUNNING) {
        record->status = DELEGATE_TASK_RUNNING;
        if (record->started_ms <= 0) {
            record->started_ms = monotonic_ms_now();
            session_record_start(record);
        } else {
            session_record_running(record);
        }
    }
    if (record->coordinator_id[0]) {
        int coord_idx = find_coordinator_index(record->coordinator_id);
        if (coord_idx >= 0) {
            refresh_coordinator_locked(&s_coordinators[coord_idx]);
            bump_coordinator_visible_revision_locked(&s_coordinators[coord_idx]);
        }
    }
    mutex_unlock(&s_delegate_mutex);
    return 0;
}

bool delegate_task_store_claim_queued_task(const char *task_id)
{
    bool claimed = false;
    if (!task_id || !task_id[0]) {
        return false;
    }

    ensure_store_init();
    mutex_lock(&s_delegate_mutex);
    int idx = find_record_index(task_id);
    if (idx >= 0) {
        delegate_task_record_t *record = &s_records[idx];
        if (record->status == DELEGATE_TASK_QUEUED) {
            record->status = DELEGATE_TASK_RUNNING;
            if (record->started_ms <= 0) {
                record->started_ms = monotonic_ms_now();
                session_record_start(record);
            } else {
                session_record_running(record);
            }
            if (record->coordinator_id[0]) {
                int coord_idx = find_coordinator_index(record->coordinator_id);
                if (coord_idx >= 0) {
                    refresh_coordinator_locked(&s_coordinators[coord_idx]);
                    bump_coordinator_visible_revision_locked(&s_coordinators[coord_idx]);
                }
            }
            claimed = true;
        }
    }
    mutex_unlock(&s_delegate_mutex);
    return claimed;
}

err_t delegate_task_store_complete(const char *task_id,
                                   const char *output,
                                   const char *target_files,
                                   bool write_approved)
{
    if (!task_id || !task_id[0]) {
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
    bool blocked_terminal = record_has_terminal_blocker(record);
    record->status = blocked_terminal ? DELEGATE_TASK_FAILED : DELEGATE_TASK_DONE;
    record->error = blocked_terminal ? ERR_FAIL : 0;
    record->finished_ms = monotonic_ms_now();
    strscpy(record->output, output ? output : "", sizeof(record->output));
    strscpy(record->target_files, target_files ? target_files : "", sizeof(record->target_files));
    record->write_approved = write_approved;
    memset(&record->pending_request, 0, sizeof(record->pending_request));
    session_record_done(record);

    if (record->coordinator_id[0]) {
        int coord_idx = find_coordinator_index(record->coordinator_id);
        if (coord_idx >= 0) {
            refresh_coordinator_locked(&s_coordinators[coord_idx]);
            bump_coordinator_visible_revision_locked(&s_coordinators[coord_idx]);
        }
    }
    mutex_unlock(&s_delegate_mutex);
    pr_info("delegate_task_store: completed task_id=%s status=%d err=%d",
            record->task_id, record->status, record->error);
    return 0;
}

err_t delegate_task_store_fail(const char *task_id,
                               err_t error,
                               const char *output)
{
    if (!task_id || !task_id[0]) {
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
    record->status = DELEGATE_TASK_FAILED;
    record->error = error;
    record->finished_ms = monotonic_ms_now();
    strscpy(record->output,
            output && output[0] ? output : err_name(error),
            sizeof(record->output));
    strscpy(record->blocker_kind, "failed", sizeof(record->blocker_kind));
    strscpy(record->blocker_text,
            output && output[0] ? output : err_name(error),
            sizeof(record->blocker_text));
    memset(&record->pending_request, 0, sizeof(record->pending_request));
    session_record_done(record);

    if (record->coordinator_id[0]) {
        int coord_idx = find_coordinator_index(record->coordinator_id);
        if (coord_idx >= 0) {
            refresh_coordinator_locked(&s_coordinators[coord_idx]);
            bump_coordinator_visible_revision_locked(&s_coordinators[coord_idx]);
        }
    }
    mutex_unlock(&s_delegate_mutex);
    pr_info("delegate_task_store: completed task_id=%s status=%d err=%d",
            record->task_id, record->status, record->error);
    return 0;
}

err_t delegate_task_store_mark_blocked(const char *task_id,
                                       const char *blocker_kind,
                                       const char *blocker_text,
                                       const char *output)
{
    if (!task_id || !task_id[0]) {
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
    record->status = DELEGATE_TASK_RUNNING;
    record->error = 0;
    if (output) {
        strscpy(record->output, output, sizeof(record->output));
    }
    strscpy(record->blocker_kind, blocker_kind ? blocker_kind : "", sizeof(record->blocker_kind));
    strscpy(record->blocker_text, blocker_text ? blocker_text : "", sizeof(record->blocker_text));
    session_record_blocked(record);

    if (record->coordinator_id[0]) {
        int coord_idx = find_coordinator_index(record->coordinator_id);
        if (coord_idx >= 0) {
            refresh_coordinator_locked(&s_coordinators[coord_idx]);
            bump_coordinator_visible_revision_locked(&s_coordinators[coord_idx]);
        }
    }
    mutex_unlock(&s_delegate_mutex);
    pr_info("delegate_task_store: blocked task_id=%s kind=%s",
            record->task_id,
            record->blocker_kind);
    return 0;
}

err_t delegate_task_store_clear_blocked(const char *task_id)
{
    if (!task_id || !task_id[0]) {
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
    bool had_blocker = record->blocker_kind[0] || record->blocker_text[0] ||
                       record->pending_request.request_type[0] ||
                       record->pending_request.prompt_text[0];
    record->blocker_kind[0] = '\0';
    record->blocker_text[0] = '\0';
    memset(&record->pending_request, 0, sizeof(record->pending_request));
    if (had_blocker) {
        session_record_unblocked(record);
    }

    if (record->coordinator_id[0]) {
        int coord_idx = find_coordinator_index(record->coordinator_id);
        if (coord_idx >= 0) {
            refresh_coordinator_locked(&s_coordinators[coord_idx]);
            bump_coordinator_visible_revision_locked(&s_coordinators[coord_idx]);
        }
    }
    mutex_unlock(&s_delegate_mutex);
    pr_info("delegate_task_store: cleared blocker task_id=%s", task_id);
    return 0;
}

err_t delegate_task_store_set_pending_request(const char *task_id,
                                              const char *request_type,
                                              const char *request_id,
                                              const char *prompt_text)
{
    if (!task_id || !task_id[0]) {
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
    strscpy(record->pending_request.request_type, request_type ? request_type : "",
            sizeof(record->pending_request.request_type));
    strscpy(record->pending_request.request_id, request_id ? request_id : "",
            sizeof(record->pending_request.request_id));
    strscpy(record->pending_request.prompt_text, prompt_text ? prompt_text : "",
            sizeof(record->pending_request.prompt_text));
    session_refresh_pending_request(record);

    if (record->coordinator_id[0]) {
        int coord_idx = find_coordinator_index(record->coordinator_id);
        if (coord_idx >= 0) {
            refresh_coordinator_locked(&s_coordinators[coord_idx]);
            bump_coordinator_visible_revision_locked(&s_coordinators[coord_idx]);
        }
    }
    mutex_unlock(&s_delegate_mutex);
    return 0;
}

err_t delegate_task_store_append_session_step(const char *task_id,
                                              const char *step_kind,
                                              const char *detail,
                                              const char *output_preview)
{
    if (!task_id || !task_id[0] || !detail || !detail[0]) {
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
    session_record_step(record, step_kind, detail, output_preview);

    if (record->coordinator_id[0]) {
        int coord_idx = find_coordinator_index(record->coordinator_id);
        if (coord_idx >= 0) {
            refresh_coordinator_locked(&s_coordinators[coord_idx]);
            bump_coordinator_visible_revision_locked(&s_coordinators[coord_idx]);
        }
    }
    mutex_unlock(&s_delegate_mutex);
    return 0;
}

err_t delegate_task_store_append_session_message(const char *task_id,
                                                 const char *message_kind,
                                                 const char *text)
{
    if (!task_id || !task_id[0] || !text || !text[0]) {
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
    session_record_message(record, message_kind, text);

    if (record->coordinator_id[0]) {
        int coord_idx = find_coordinator_index(record->coordinator_id);
        if (coord_idx >= 0) {
            refresh_coordinator_locked(&s_coordinators[coord_idx]);
            bump_coordinator_visible_revision_locked(&s_coordinators[coord_idx]);
        }
    }
    mutex_unlock(&s_delegate_mutex);
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
                                            const char *chat_id,
                                            const char *team_run_id,
                                            const char *team_name,
                                            const char *dispatch_mode)
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
    strscpy(s_coordinators[idx].team_run_id, team_run_id ? team_run_id : "", sizeof(s_coordinators[idx].team_run_id));
    strscpy(s_coordinators[idx].team_name, team_name ? team_name : "", sizeof(s_coordinators[idx].team_name));
    strscpy(s_coordinators[idx].dispatch_mode, dispatch_mode ? dispatch_mode : "", sizeof(s_coordinators[idx].dispatch_mode));
    strscpy(s_coordinators[idx].status, "running", sizeof(s_coordinators[idx].status));
    s_coordinators[idx].visible_revision = s_visible_revision_seq++;
    s_coordinators[idx].changed = true;
    s_coordinators[idx].completion_notified = false;
    s_coordinators[idx].parent_response_sent = false;
    s_coordinators[idx].parent_resume_enqueued = false;
    s_coordinators[idx].last_sent_revision = 0;
    s_coordinators[idx].wake_state = DELEGATE_WAKE_IDLE;
    s_coordinators[idx].wake_retry_count = 0;
    s_coordinators[idx].wake_last_attempt_ms = 0;
    s_coordinators[idx].wake_last_success_ms = 0;
    s_coordinators[idx].wake_last_error[0] = '\0';
    s_coordinators[idx].blocker_kind[0] = '\0';
    s_coordinators[idx].blocker_text[0] = '\0';
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
    strscpy(view->session_id, s_records[task_idx].session_id, sizeof(view->session_id));
    strscpy(view->task_key, s_records[task_idx].task_key, sizeof(view->task_key));
    strscpy(view->description, s_records[task_idx].description, sizeof(view->description));
    strscpy(view->subagent_type, s_records[task_idx].subagent_type, sizeof(view->subagent_type));
    strscpy(view->model, s_records[task_idx].model, sizeof(view->model));
    strscpy(view->scope_path, s_records[task_idx].scope_path, sizeof(view->scope_path));
    strscpy(view->scope_kind, s_records[task_idx].scope_kind, sizeof(view->scope_kind));
    strscpy(view->analysis_focus, s_records[task_idx].analysis_focus, sizeof(view->analysis_focus));
    strscpy(view->depends_on, s_records[task_idx].depends_on, sizeof(view->depends_on));
    view->preflight_tool = s_records[task_idx].preflight_tool;
    strscpy(view->status, task_status_name(s_records[task_idx].status), sizeof(view->status));
    pr_info("delegate_store attach_task: coordinator=%s slot=%d task_id=%s desc=%s",
            coordinator_id,
            coordinator->agent_count - 1,
            view->task_id,
            view->description);
    refresh_coordinator_locked(coordinator);
    bump_coordinator_visible_revision_locked(coordinator);
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

err_t delegate_task_store_mark_parent_response_sent(const char *chat_id)
{
    if (!chat_id || !chat_id[0]) {
        return ERR_INVALID_ARG;
    }

    ensure_store_init();
    mutex_lock(&s_delegate_mutex);
    for (int i = 0; i < DELEGATE_COORDINATOR_STORE_MAX; i++) {
        if (!s_coordinators[i].coordinator_id[0]) {
            continue;
        }
        if (strcmp(s_coordinators[i].chat_id, chat_id) != 0) {
            continue;
        }
        s_coordinators[i].parent_response_sent = true;
        bump_coordinator_visible_revision_locked(&s_coordinators[i]);
    }
    mutex_unlock(&s_delegate_mutex);
    return 0;
}

err_t delegate_task_store_mark_parent_resume_enqueued(const char *coordinator_id)
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
    s_coordinators[coord_idx].parent_resume_enqueued = true;
    mutex_unlock(&s_delegate_mutex);
    return 0;
}

err_t delegate_task_store_mark_visible_revision_sent(const char *coordinator_id,
                                                     unsigned long visible_revision)
{
    if (!coordinator_id || !coordinator_id[0] || visible_revision == 0) {
        return ERR_INVALID_ARG;
    }

    ensure_store_init();
    mutex_lock(&s_delegate_mutex);
    int coord_idx = find_coordinator_index(coordinator_id);
    if (coord_idx < 0) {
        mutex_unlock(&s_delegate_mutex);
        return ERR_NOT_FOUND;
    }
    if (s_coordinators[coord_idx].visible_revision == visible_revision) {
        s_coordinators[coord_idx].last_sent_revision = visible_revision;
    }
    mutex_unlock(&s_delegate_mutex);
    return 0;
}

err_t delegate_task_store_mark_wake_pending(const char *coordinator_id)
{
    if (!coordinator_id || !coordinator_id[0]) {
        return ERR_INVALID_ARG;
    }

    ensure_store_init();
    mutex_lock(&s_delegate_mutex);
    err_t err = mutate_wake_state_locked(coordinator_id, DELEGATE_WAKE_PENDING, false, 0, false);
    mutex_unlock(&s_delegate_mutex);
    return err;
}

err_t delegate_task_store_mark_wake_dispatched(const char *coordinator_id)
{
    if (!coordinator_id || !coordinator_id[0]) {
        return ERR_INVALID_ARG;
    }

    ensure_store_init();
    mutex_lock(&s_delegate_mutex);
    err_t err = mutate_wake_state_locked(coordinator_id, DELEGATE_WAKE_DISPATCHED, false, 0, true);
    mutex_unlock(&s_delegate_mutex);
    return err;
}

err_t delegate_task_store_mark_wake_retry(const char *coordinator_id, err_t error)
{
    if (!coordinator_id || !coordinator_id[0]) {
        return ERR_INVALID_ARG;
    }

    ensure_store_init();
    mutex_lock(&s_delegate_mutex);
    err_t err = mutate_wake_state_locked(coordinator_id, DELEGATE_WAKE_PENDING, true, error, false);
    mutex_unlock(&s_delegate_mutex);
    return err;
}

err_t delegate_task_store_mark_wake_completed(const char *coordinator_id)
{
    if (!coordinator_id || !coordinator_id[0]) {
        return ERR_INVALID_ARG;
    }

    ensure_store_init();
    mutex_lock(&s_delegate_mutex);
    err_t err = mutate_wake_state_locked(coordinator_id, DELEGATE_WAKE_COMPLETED, false, 0, true);
    mutex_unlock(&s_delegate_mutex);
    return err;
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
