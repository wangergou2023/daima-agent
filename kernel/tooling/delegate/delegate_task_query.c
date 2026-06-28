#include "delegate_task_store_internal.h"

#include <string.h>

#include "linux/kernel.h"
#include "linux/printk.h"
#include "text.h"

static bool coordinator_is_terminal_and_fully_visible(const delegate_coordinator_record_t *coordinator)
{
    if (!coordinator || !coordinator->coordinator_id[0]) {
        return false;
    }
    if (coordinator->queued_count > 0 || coordinator->running_count > 0) {
        return false;
    }
    if (strcmp(coordinator->status, "done") != 0 &&
        strcmp(coordinator->status, "failed") != 0) {
        return false;
    }
    if (coordinator->visible_revision == 0 ||
        coordinator->last_sent_revision != coordinator->visible_revision) {
        return false;
    }
    if (!coordinator->parent_resume_enqueued &&
        coordinator->completion_notified) {
        return false;
    }
    return true;
}

static void refresh_coordinator_if_needed_locked(delegate_coordinator_record_t *coordinator)
{
    if (!coordinator || !coordinator->coordinator_id[0]) {
        return;
    }
    if (coordinator_is_terminal_and_fully_visible(coordinator)) {
        return;
    }
    refresh_coordinator_locked(coordinator);
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
        if (!coordinator->changed) {
            continue;
        }
        for (int j = 0; j < coordinator->agent_count; j++) {
            int task_idx = find_record_index(coordinator->agents[j].task_id);
            if (task_idx >= 0) {
                poll_record_locked(&s_records[task_idx]);
            }
        }
        refresh_coordinator_if_needed_locked(coordinator);
        if (!coordinator->changed || !coordinator->parent_response_sent) {
            continue;
        }
        out[emitted++] = *coordinator;
        coordinator->changed = false;
        found = true;
    }
    mutex_unlock(&s_delegate_mutex);
    return found;
}

bool delegate_task_store_drain_changed_coordinators(delegate_coordinator_record_t *out,
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
        if (!coordinator->changed) {
            continue;
        }
        for (int j = 0; j < coordinator->agent_count; j++) {
            int task_idx = find_record_index(coordinator->agents[j].task_id);
            if (task_idx >= 0) {
                poll_record_locked(&s_records[task_idx]);
            }
        }
        refresh_coordinator_if_needed_locked(coordinator);
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

bool delegate_task_store_peek_changed_coordinators(delegate_coordinator_record_t *out,
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
        if (!coordinator->changed) {
            continue;
        }
        for (int j = 0; j < coordinator->agent_count; j++) {
            int task_idx = find_record_index(coordinator->agents[j].task_id);
            if (task_idx >= 0) {
                poll_record_locked(&s_records[task_idx]);
            }
        }
        refresh_coordinator_if_needed_locked(coordinator);
        if (!coordinator->changed) {
            continue;
        }
        out[emitted++] = *coordinator;
        found = true;
    }
    mutex_unlock(&s_delegate_mutex);
    return found;
}

bool delegate_task_store_list_active_coordinators(delegate_coordinator_record_t *out,
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
        if (strcmp(coordinator->status, "done") == 0 ||
            strcmp(coordinator->status, "failed") == 0) {
            continue;
        }
        for (int j = 0; j < coordinator->agent_count; j++) {
            int task_idx = find_record_index(coordinator->agents[j].task_id);
            if (task_idx >= 0) {
                poll_record_locked(&s_records[task_idx]);
            }
        }
        out[emitted++] = *coordinator;
        found = true;
    }
    mutex_unlock(&s_delegate_mutex);
    return found;
}

err_t delegate_task_store_snapshot_coordinator(const char *coordinator_id,
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
    refresh_coordinator_if_needed_locked(coordinator);
    *out = *coordinator;
    mutex_unlock(&s_delegate_mutex);
    return 0;
}

err_t delegate_task_store_snapshot_parent(const char *chat_id,
                                          delegate_parent_registry_view_t *out)
{
    if (!chat_id || !chat_id[0] || !out) {
        return ERR_INVALID_ARG;
    }

    ensure_store_init();
    memset(out, 0, sizeof(*out));
    strscpy(out->chat_id, chat_id, sizeof(out->chat_id));

    mutex_lock(&s_delegate_mutex);

    for (int i = 0; i < DELEGATE_COORDINATOR_STORE_MAX; i++) {
        delegate_coordinator_record_t *coordinator = &s_coordinators[i];
        if (!coordinator->coordinator_id[0] || strcmp(coordinator->chat_id, chat_id) != 0) {
            continue;
        }
        for (int j = 0; j < coordinator->agent_count; j++) {
            int task_idx = find_record_index(coordinator->agents[j].task_id);
            if (task_idx >= 0) {
                poll_record_locked(&s_records[task_idx]);
            }
        }
        refresh_coordinator_if_needed_locked(coordinator);
        if (out->coordinator_count < DELEGATE_COORDINATOR_STORE_MAX) {
            delegate_parent_coordinator_list_item_t *item =
                &out->coordinators[out->coordinator_count++];
            memset(item, 0, sizeof(*item));
            strscpy(item->chat_id, coordinator->chat_id, sizeof(item->chat_id));
            strscpy(item->coordinator_id, coordinator->coordinator_id, sizeof(item->coordinator_id));
            strscpy(item->team_run_id, coordinator->team_run_id, sizeof(item->team_run_id));
            strscpy(item->team_name, coordinator->team_name, sizeof(item->team_name));
            strscpy(item->dispatch_mode, coordinator->dispatch_mode, sizeof(item->dispatch_mode));
            strscpy(item->status, coordinator->status, sizeof(item->status));
            item->agent_count = coordinator->agent_count;
            item->completed_count = coordinator->completed_count;
            item->running_count = coordinator->running_count;
            item->queued_count = coordinator->queued_count;
            item->blocked_count = coordinator->blocked_count;
            item->failed_count = coordinator->failed_count;
            item->effective_output_count = coordinator->effective_output_count;
            item->completion_notified = coordinator->completion_notified;
            item->parent_response_sent = coordinator->parent_response_sent;
            item->parent_resume_enqueued = coordinator->parent_resume_enqueued;
            item->wake_state = coordinator->wake_state;
            item->wake_retry_count = coordinator->wake_retry_count;
            item->wake_last_attempt_ms = coordinator->wake_last_attempt_ms;
            item->wake_last_success_ms = coordinator->wake_last_success_ms;
            strscpy(item->wake_last_error, coordinator->wake_last_error, sizeof(item->wake_last_error));
        }
    }

    for (int i = 0; i < DELEGATE_TASK_STORE_MAX; i++) {
        delegate_task_record_t *record = &s_records[i];
        if (!record->task_id[0] || !record->coordinator_id[0]) {
            continue;
        }

        bool matches_parent = false;
        for (int j = 0; j < out->coordinator_count; j++) {
            if (strcmp(out->coordinators[j].coordinator_id, record->coordinator_id) == 0) {
                matches_parent = true;
                break;
            }
        }
        if (!matches_parent) {
            continue;
        }
        poll_record_locked(record);
        if (out->task_count < DELEGATE_TASK_STORE_MAX) {
            delegate_parent_task_list_item_t *item = &out->tasks[out->task_count++];
            memset(item, 0, sizeof(*item));
            strscpy(item->task_id, record->task_id, sizeof(item->task_id));
            strscpy(item->coordinator_id, record->coordinator_id, sizeof(item->coordinator_id));
            strscpy(item->session_id, record->session_id, sizeof(item->session_id));
            strscpy(item->subagent_type, record->subagent_type, sizeof(item->subagent_type));
            strscpy(item->task_key, record->task_key, sizeof(item->task_key));
            strscpy(item->description, record->description, sizeof(item->description));
            strscpy(item->model, record->model, sizeof(item->model));
            strscpy(item->scope_path, record->scope_path, sizeof(item->scope_path));
            strscpy(item->scope_kind, record->scope_kind, sizeof(item->scope_kind));
            strscpy(item->analysis_focus, record->analysis_focus, sizeof(item->analysis_focus));
            strscpy(item->depends_on, record->depends_on, sizeof(item->depends_on));
            item->started_ms = record->started_ms;
            item->finished_ms = record->finished_ms;
            item->write_approved = record->write_approved;
            strscpy(item->target_files, record->target_files, sizeof(item->target_files));
            strscpy(item->blocker_kind, record->blocker_kind, sizeof(item->blocker_kind));
            strscpy(item->blocker_text, record->blocker_text, sizeof(item->blocker_text));
            strscpy(item->output, record->output, sizeof(item->output));
            strscpy(item->status, task_status_name(record->status), sizeof(item->status));
            if (record->status == DELEGATE_TASK_FAILED) {
                strscpy(item->status, "failed", sizeof(item->status));
            }
            if (record->started_ms > 0) {
                long end_ms = record->finished_ms > 0 ? record->finished_ms : monotonic_ms_now();
                item->elapsed_ms = end_ms > record->started_ms ? (end_ms - record->started_ms) : 0;
            }
        }
    }

    mutex_unlock(&s_delegate_mutex);
    return (out->coordinator_count > 0 || out->task_count > 0) ? 0 : ERR_NOT_FOUND;
}

err_t delegate_task_store_find_by_session(const char *session_id,
                                          delegate_task_record_t *task_out,
                                          delegate_coordinator_record_t *coordinator_out)
{
    if (!session_id || !session_id[0]) {
        return ERR_INVALID_ARG;
    }

    ensure_store_init();
    mutex_lock(&s_delegate_mutex);
    int task_idx = find_record_index_by_session(session_id);
    if (task_idx < 0) {
        mutex_unlock(&s_delegate_mutex);
        return ERR_NOT_FOUND;
    }

    delegate_task_record_t *record = &s_records[task_idx];
    if (task_out) {
        *task_out = *record;
    }
    if (coordinator_out) {
        memset(coordinator_out, 0, sizeof(*coordinator_out));
        if (record->coordinator_id[0]) {
            int coord_idx = find_coordinator_index(record->coordinator_id);
            if (coord_idx >= 0) {
                refresh_coordinator_locked(&s_coordinators[coord_idx]);
                *coordinator_out = s_coordinators[coord_idx];
            }
        }
    }
    mutex_unlock(&s_delegate_mutex);
    return 0;
}

err_t delegate_task_store_find_parent_route_by_session(const char *session_id,
                                                       delegate_parent_route_view_t *out)
{
    if (!session_id || !session_id[0] || !out) {
        return ERR_INVALID_ARG;
    }

    ensure_store_init();
    mutex_lock(&s_delegate_mutex);
    int task_idx = find_record_index_by_session(session_id);
    if (task_idx < 0) {
        mutex_unlock(&s_delegate_mutex);
        return ERR_NOT_FOUND;
    }

    delegate_task_record_t *record = &s_records[task_idx];
    memset(out, 0, sizeof(*out));
    strscpy(out->task_id, record->task_id, sizeof(out->task_id));
    strscpy(out->session_id, record->session_id, sizeof(out->session_id));
    strscpy(out->coordinator_id, record->coordinator_id, sizeof(out->coordinator_id));
    if (record->coordinator_id[0]) {
        int coord_idx = find_coordinator_index(record->coordinator_id);
        if (coord_idx >= 0) {
            strscpy(out->parent_chat_id, s_coordinators[coord_idx].chat_id, sizeof(out->parent_chat_id));
        }
    }
    mutex_unlock(&s_delegate_mutex);
    return out->parent_chat_id[0] ? 0 : ERR_NOT_FOUND;
}

int delegate_task_store_running_count(void)
{
    int running = 0;

    ensure_store_init();
    mutex_lock(&s_delegate_mutex);
    for (int i = 0; i < DELEGATE_TASK_STORE_MAX; i++) {
        if (!s_records[i].task_id[0] || s_records[i].status != DELEGATE_TASK_RUNNING) {
            continue;
        }
        if (s_records[i].blocker_kind[0] || s_records[i].blocker_text[0] ||
            (s_records[i].pending_request.request_type[0] &&
             s_records[i].pending_request.prompt_text[0])) {
            continue;
        }
        {
            running++;
        }
    }
    mutex_unlock(&s_delegate_mutex);
    return running;
}

int delegate_task_store_running_count_for_parent(const char *chat_id)
{
    int running = 0;

    if (!chat_id || !chat_id[0]) {
        return 0;
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
        refresh_coordinator_if_needed_locked(&s_coordinators[i]);
        running += s_coordinators[i].running_count;
    }
    mutex_unlock(&s_delegate_mutex);
    return running;
}

int delegate_task_store_pending_coordinator_count(void)
{
    int pending = 0;

    ensure_store_init();
    mutex_lock(&s_delegate_mutex);
    for (int i = 0; i < DELEGATE_COORDINATOR_STORE_MAX; i++) {
        delegate_coordinator_record_t *coordinator = &s_coordinators[i];
        if (!coordinator->coordinator_id[0]) {
            continue;
        }
        refresh_coordinator_if_needed_locked(coordinator);
        if (coordinator->queued_count > 0 || coordinator->running_count > 0) {
            pending++;
            continue;
        }
        if (coordinator->completion_notified && !coordinator->parent_resume_enqueued) {
            pending++;
        }
    }
    mutex_unlock(&s_delegate_mutex);
    return pending;
}
