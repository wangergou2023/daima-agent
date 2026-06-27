#include "delegate_task_store_internal.h"

#include <string.h>

#include "drivers/tool/tool_delegate_result_json.h"
#include "linux/kernel.h"
#include "linux/printk.h"
#include "text.h"

static const char *delegate_projection_visible_text(const char *text,
                                                    char *rendered,
                                                    size_t rendered_size)
{
    if (!text || !text[0]) {
        return "";
    }
    if (rendered && rendered_size > 0 &&
        tool_delegate_parse_result_json_rendered(text, rendered, rendered_size)) {
        return rendered;
    }
    return text;
}

static void delegate_projection_preview_text(const char *text,
                                             char *preview,
                                             size_t preview_size,
                                             int budget)
{
    char rendered[2048];
    const char *visible = delegate_projection_visible_text(text, rendered, sizeof(rendered));

    if (!preview || preview_size == 0) {
        return;
    }
    preview[0] = '\0';
    if (!visible || !visible[0]) {
        return;
    }
    text_shorten(visible, preview, preview_size, budget);
}

static void session_event_make_id(char *buf,
                                  size_t size,
                                  const char *session_id,
                                  const char *kind,
                                  const char *phase,
                                  const char *status,
                                  const char *label,
                                  const char *detail,
                                  long ts_ms,
                                  int ordinal)
{
    unsigned long hash = 2166136261u;
    const unsigned char *ptr = NULL;
    const char *parts[] = {
        session_id ? session_id : "",
        kind ? kind : "",
        phase ? phase : "",
        status ? status : "",
        label ? label : "",
        detail ? detail : "",
    };

    if (!buf || size == 0) {
        return;
    }

    for (size_t part_idx = 0; part_idx < sizeof(parts) / sizeof(parts[0]); part_idx++) {
        for (ptr = (const unsigned char *)parts[part_idx]; ptr && *ptr; ptr++) {
            hash ^= (unsigned long)(*ptr);
            hash *= 16777619u;
        }
        hash ^= (unsigned long)'|';
        hash *= 16777619u;
    }

    snprintf(buf, size, "sess-%s-%ld-%d-%08lx",
             session_id && session_id[0] ? session_id : "delegate",
             ts_ms > 0 ? ts_ms : monotonic_ms_now(),
             ordinal,
             hash);
}

static void session_summary_set(delegate_child_session_view_t *session, const char *text)
{
    char rendered[2048];

    if (!session) {
        return;
    }
    strscpy(session->summary,
            delegate_projection_visible_text(text, rendered, sizeof(rendered)),
            sizeof(session->summary));
}

static void session_pending_reset(delegate_child_session_view_t *session)
{
    if (!session) {
        return;
    }
    session->permission_count = 0;
    session->question_count = 0;
    memset(session->permissions, 0, sizeof(session->permissions));
    memset(session->questions, 0, sizeof(session->questions));
}

static void session_pending_push(delegate_session_pending_request_t *items,
                                 int *count,
                                 size_t capacity,
                                 const char *request_type,
                                 const char *request_id,
                                 const char *prompt_text)
{
    delegate_session_pending_request_t *item = NULL;

    if (!items || !count || !capacity || !request_type || !request_type[0] ||
        !prompt_text || !prompt_text[0]) {
        return;
    }
    if (*count < 0) {
        *count = 0;
    }
    if ((size_t)(*count) >= capacity) {
        *count = (int)capacity - 1;
    }
    item = &items[*count];
    memset(item, 0, sizeof(*item));
    strscpy(item->request_type, request_type, sizeof(item->request_type));
    strscpy(item->request_id, request_id ? request_id : "", sizeof(item->request_id));
    strscpy(item->prompt_text, prompt_text, sizeof(item->prompt_text));
    (*count)++;
}

static void session_pending_set_blocker(delegate_child_session_view_t *session,
                                        const char *blocker_kind,
                                        const char *blocker_text)
{
    if (!session) {
        return;
    }
    session_pending_reset(session);
    if (!blocker_kind || !blocker_kind[0] || !blocker_text || !blocker_text[0]) {
        return;
    }
    if (strcmp(blocker_kind, "permission") == 0) {
        session_pending_push(session->permissions,
                             &session->permission_count,
                             ARRAY_SIZE(session->permissions),
                             "permission",
                             "",
                             blocker_text);
        return;
    }
    if (strcmp(blocker_kind, "question") == 0) {
        session_pending_push(session->questions,
                             &session->question_count,
                             ARRAY_SIZE(session->questions),
                             "question",
                             "",
                             blocker_text);
    }
}

static void session_pending_set_request(delegate_child_session_view_t *session,
                                        const delegate_pending_request_view_t *pending)
{
    if (!session || !pending || !pending->request_type[0] || !pending->prompt_text[0]) {
        return;
    }

    session_pending_reset(session);
    if (strcmp(pending->request_type, "question") == 0 ||
        strcmp(pending->request_type, "question_text") == 0) {
        session_pending_push(session->questions,
                             &session->question_count,
                             ARRAY_SIZE(session->questions),
                             pending->request_type,
                             pending->request_id,
                             pending->prompt_text);
        return;
    }

    session_pending_push(session->permissions,
                         &session->permission_count,
                         ARRAY_SIZE(session->permissions),
                         pending->request_type,
                         pending->request_id,
                         pending->prompt_text);
}

static void session_append_frame(delegate_child_session_view_t *session,
                                 const char *session_id,
                                 const char *type,
                                 const char *status,
                                 const char *phase,
                                 const char *task,
                                 const char *detail,
                                 const char *output_preview,
                                 const char *blocker_kind,
                                 const char *blocker_text,
                                 long ts_ms)
{
    if (!session) {
        return;
    }
    if (session->frame_count >= DELEGATE_SESSION_FRAME_LIMIT) {
        memmove(&session->frames[0],
                &session->frames[1],
                sizeof(session->frames[0]) * (DELEGATE_SESSION_FRAME_LIMIT - 1));
        session->frame_count = DELEGATE_SESSION_FRAME_LIMIT - 1;
    }
    delegate_session_frame_t *frame = &session->frames[session->frame_count++];
    memset(frame, 0, sizeof(*frame));
    if (session->frame_seq_next == 0) {
        session->frame_seq_next = 1;
    }
    frame->seq = session->frame_seq_next++;
    session_event_make_id(frame->id,
                          sizeof(frame->id),
                          session_id,
                          type,
                          phase,
                          status,
                          task,
                          detail,
                          ts_ms > 0 ? ts_ms : monotonic_ms_now(),
                          (int)frame->seq);
    strscpy(frame->type, type ? type : "", sizeof(frame->type));
    strscpy(frame->status, status ? status : "", sizeof(frame->status));
    strscpy(frame->phase, phase ? phase : "", sizeof(frame->phase));
    strscpy(frame->task, task ? task : "", sizeof(frame->task));
    strscpy(frame->detail, detail ? detail : "", sizeof(frame->detail));
    strscpy(frame->output_preview, output_preview ? output_preview : "", sizeof(frame->output_preview));
    strscpy(frame->blocker_kind, blocker_kind ? blocker_kind : "", sizeof(frame->blocker_kind));
    strscpy(frame->blocker_text, blocker_text ? blocker_text : "", sizeof(frame->blocker_text));
    frame->ts_ms = ts_ms > 0 ? ts_ms : monotonic_ms_now();
}

static void session_append_commit(delegate_child_session_view_t *session,
                                  const char *session_id,
                                  const char *kind,
                                  const char *phase,
                                  const char *status,
                                  const char *label,
                                  const char *text,
                                  long ts_ms)
{
    if (!session || !text || !text[0]) {
        return;
    }
    if (session->commit_count >= DELEGATE_SESSION_COMMIT_LIMIT) {
        memmove(&session->commits[0],
                &session->commits[1],
                sizeof(session->commits[0]) * (DELEGATE_SESSION_COMMIT_LIMIT - 1));
        session->commit_count = DELEGATE_SESSION_COMMIT_LIMIT - 1;
    }
    delegate_session_commit_t *commit = &session->commits[session->commit_count++];
    memset(commit, 0, sizeof(*commit));
    if (session->commit_seq_next == 0) {
        session->commit_seq_next = 1;
    }
    commit->seq = session->commit_seq_next++;
    session_event_make_id(commit->id,
                          sizeof(commit->id),
                          session_id,
                          kind,
                          phase,
                          status,
                          label,
                          text,
                          ts_ms > 0 ? ts_ms : monotonic_ms_now(),
                          (int)commit->seq);
    strscpy(commit->kind, kind ? kind : "", sizeof(commit->kind));
    strscpy(commit->phase, phase ? phase : "", sizeof(commit->phase));
    strscpy(commit->status, status ? status : "", sizeof(commit->status));
    strscpy(commit->label, label ? label : "", sizeof(commit->label));
    strscpy(commit->text, text ? text : "", sizeof(commit->text));
    commit->ts_ms = ts_ms > 0 ? ts_ms : monotonic_ms_now();
}

static void session_record_pending_request_frame(delegate_task_record_t *record)
{
    const char *kind = "permission";
    char preview[256];
    long ts_ms;

    if (!record || !record->pending_request.request_type[0] || !record->pending_request.prompt_text[0]) {
        return;
    }

    if (strcmp(record->pending_request.request_type, "question") == 0 ||
        strcmp(record->pending_request.request_type, "question_text") == 0) {
        kind = "question";
    }

    delegate_projection_preview_text(record->output, preview, sizeof(preview), 220);
    ts_ms = monotonic_ms_now();
    session_append_frame(&record->child_session,
                         record->session_id,
                         "subagent_request", "blocked", "blocked",
                         record->description,
                         record->pending_request.prompt_text,
                         preview,
                         kind,
                         record->pending_request.prompt_text,
                         ts_ms);
    session_append_commit(&record->child_session,
                          record->session_id,
                          kind, "blocked", "blocked",
                          record->description,
                          record->pending_request.prompt_text,
                          ts_ms);
}

void session_record_step(delegate_task_record_t *record,
                         const char *step_kind,
                         const char *detail,
                         const char *output_preview)
{
    char preview[256];
    long ts_ms;

    if (!record || !detail || !detail[0]) {
        return;
    }

    delegate_projection_preview_text(output_preview, preview, sizeof(preview), 220);
    ts_ms = monotonic_ms_now();
    session_summary_set(&record->child_session, detail);
    session_append_frame(&record->child_session,
                         record->session_id,
                         "subagent_step", "running", "progress",
                         record->description,
                         detail,
                         preview,
                         step_kind ? step_kind : "",
                         "",
                         ts_ms);
    session_append_commit(&record->child_session,
                          record->session_id,
                          step_kind && step_kind[0] ? step_kind : "progress",
                          "running",
                          "running",
                          record->description,
                          detail,
                          ts_ms);
}

void session_record_message(delegate_task_record_t *record,
                            const char *message_kind,
                            const char *text)
{
    char preview[256];
    const char *kind = message_kind && message_kind[0] ? message_kind : "assistant";
    long ts_ms;

    if (!record || !text || !text[0]) {
        return;
    }

    delegate_projection_preview_text(text, preview, sizeof(preview), 220);
    session_summary_set(&record->child_session, preview[0] ? preview : text);
    ts_ms = monotonic_ms_now();
    session_append_frame(&record->child_session,
                         record->session_id,
                         "subagent_message", "running", "message",
                         record->description,
                         preview[0] ? preview : text,
                         "",
                         kind,
                         "",
                         ts_ms);
    session_append_commit(&record->child_session,
                          record->session_id,
                          kind, "message", "running",
                          record->description,
                          preview[0] ? preview : text,
                          ts_ms);
}

void session_record_start(delegate_task_record_t *record)
{
    char detail[192];
    if (!record) {
        return;
    }
    snprintf(detail, sizeof(detail), "model=%s · started",
             record->model[0] ? record->model : "unknown");
    session_summary_set(&record->child_session, detail);
    session_append_frame(&record->child_session,
                         record->session_id,
                         "subagent_start", "running", "start",
                         record->description, detail, "", "", "", record->started_ms);
    session_append_commit(&record->child_session,
                          record->session_id,
                          "start", "running", "running",
                          record->description, detail, record->started_ms);
}

void session_record_queued(delegate_task_record_t *record)
{
    if (!record) {
        return;
    }
    session_summary_set(&record->child_session, "queued");
    session_append_frame(&record->child_session,
                         record->session_id,
                         "subagent_progress", "queued", "queued",
                         record->description, "queued", "", "", "", monotonic_ms_now());
}

void session_record_running(delegate_task_record_t *record)
{
    char detail[192];
    if (!record) {
        return;
    }
    snprintf(detail, sizeof(detail), "model=%s · running",
             record->model[0] ? record->model : "unknown");
    session_summary_set(&record->child_session, detail);
    session_append_frame(&record->child_session,
                         record->session_id,
                         "subagent_progress", "running", "progress",
                         record->description, detail, "", "", "", monotonic_ms_now());
}

void session_record_blocked(delegate_task_record_t *record)
{
    char preview[256];
    char blocker_preview[256];
    long ts_ms;
    if (!record) {
        return;
    }
    delegate_projection_preview_text(record->output, preview, sizeof(preview), 220);
    delegate_projection_preview_text(record->blocker_text, blocker_preview, sizeof(blocker_preview), 220);
    session_summary_set(&record->child_session,
                        blocker_preview[0] ? blocker_preview : preview);
    if (record->pending_request.request_type[0] && record->pending_request.prompt_text[0]) {
        session_pending_set_request(&record->child_session, &record->pending_request);
    } else {
        session_pending_set_blocker(&record->child_session, record->blocker_kind, record->blocker_text);
    }
    ts_ms = monotonic_ms_now();
    session_append_frame(&record->child_session,
                         record->session_id,
                         "subagent_blocked", "blocked", "blocked",
                         record->description,
                         blocker_preview[0] ? blocker_preview : record->blocker_text,
                         preview,
                         record->blocker_kind,
                         blocker_preview[0] ? blocker_preview : record->blocker_text,
                         ts_ms);
    session_append_commit(&record->child_session,
                          record->session_id,
                          "blocker", "blocked", "blocked",
                          record->description,
                          blocker_preview[0] ? blocker_preview : preview,
                          ts_ms);
}

void session_refresh_pending_request(delegate_task_record_t *record)
{
    if (!record) {
        return;
    }

    if (record->pending_request.request_type[0] && record->pending_request.prompt_text[0]) {
        session_summary_set(&record->child_session, record->pending_request.prompt_text);
        session_pending_set_request(&record->child_session, &record->pending_request);
        session_record_pending_request_frame(record);
        return;
    }

    session_pending_reset(&record->child_session);
}

void session_record_unblocked(delegate_task_record_t *record)
{
    char preview[256];
    long ts_ms;
    if (!record) {
        return;
    }
    delegate_projection_preview_text(record->output, preview, sizeof(preview), 220);
    session_pending_reset(&record->child_session);
    session_summary_set(&record->child_session, preview[0] ? preview : "subagent resumed");
    ts_ms = monotonic_ms_now();
    session_append_frame(&record->child_session,
                         record->session_id,
                         "subagent_unblocked", "running", "resumed",
                         record->description, "blocker resolved", preview, "", "", ts_ms);
    session_append_commit(&record->child_session,
                          record->session_id,
                          "resume", "resumed", "running",
                          record->description,
                          preview[0] ? preview : "blocker resolved",
                          ts_ms);
}

void session_record_done(delegate_task_record_t *record)
{
    char preview[256];
    const char *status = "done";
    const char *phase = "done";
    long ts_ms;
    if (!record) {
        return;
    }
    delegate_projection_preview_text(record->output, preview, sizeof(preview), 220);
    session_pending_reset(&record->child_session);
    session_summary_set(&record->child_session, preview[0] ? preview : record->output);
    if (record->status == DELEGATE_TASK_FAILED) {
        status = "failed";
        phase = "failed";
    }
    ts_ms = record->finished_ms > 0 ? record->finished_ms : monotonic_ms_now();
    session_append_frame(&record->child_session,
                         record->session_id,
                         "subagent_done", status, phase,
                         record->description, preview[0] ? preview : record->output,
                         preview, record->blocker_kind, record->blocker_text, ts_ms);
    session_append_commit(&record->child_session,
                          record->session_id,
                          "result", phase, status,
                          record->description, preview[0] ? preview : record->output, ts_ms);
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

void poll_record_locked(delegate_task_record_t *record)
{
    (void)record;
}

void refresh_coordinator_locked(delegate_coordinator_record_t *coordinator)
{
    if (!coordinator || !coordinator->coordinator_id[0]) {
        return;
    }

    int completed_count = 0;
    int running_count = 0;
    int queued_count = 0;
    int blocked_count = 0;
    int failed_count = 0;
    int effective_output_count = 0;
    bool any_running = false;
    bool any_queued = false;
    bool any_failed = false;
    bool any_blocked = false;
    const char *coordinator_blocker_kind = "";
    const char *coordinator_blocker_text = "";

    refresh_implement_write_approvals_locked(coordinator);

    for (int i = 0; i < coordinator->agent_count; i++) {
        delegate_coordinator_agent_view_t *view = &coordinator->agents[i];
        int task_idx = find_record_index(view->task_id);
        if (task_idx < 0) {
            continue;
        }
        delegate_task_record_t *record = &s_records[task_idx];
        strscpy(view->session_id, record->session_id, sizeof(view->session_id));
        strscpy(view->subagent_type, record->subagent_type, sizeof(view->subagent_type));
        strscpy(view->task_key, record->task_key, sizeof(view->task_key));
        strscpy(view->description, record->description, sizeof(view->description));
        strscpy(view->model, record->model, sizeof(view->model));
        strscpy(view->scope_path, record->scope_path, sizeof(view->scope_path));
        strscpy(view->scope_kind, record->scope_kind, sizeof(view->scope_kind));
        strscpy(view->analysis_focus, record->analysis_focus, sizeof(view->analysis_focus));
        strscpy(view->depends_on, record->depends_on, sizeof(view->depends_on));
        view->preflight_tool = record->preflight_tool;
        strscpy(view->target_files, record->target_files, sizeof(view->target_files));
        strscpy(view->blocker_kind, record->blocker_kind, sizeof(view->blocker_kind));
        strscpy(view->blocker_text, record->blocker_text, sizeof(view->blocker_text));
        view->write_approved = record->write_approved;
        if (record->started_ms > 0) {
            long end_ms = record->finished_ms > 0 ? record->finished_ms : monotonic_ms_now();
            view->elapsed_ms = end_ms > record->started_ms ? (end_ms - record->started_ms) : 0;
        } else {
            view->elapsed_ms = 0;
        }

        if (record->status == DELEGATE_TASK_DONE) {
            strscpy(view->status, "done", sizeof(view->status));
            completed_count++;
            if (text_has_effective_output(record->output)) {
                effective_output_count++;
            }
        } else if (record->status == DELEGATE_TASK_FAILED) {
            strscpy(view->status, "error", sizeof(view->status));
            completed_count++;
            any_failed = true;
            failed_count++;
            if (record->blocker_kind[0] || record->blocker_text[0]) {
                any_blocked = true;
                coordinator_blocker_kind = record->blocker_kind;
                coordinator_blocker_text = record->blocker_text;
            }
        } else if (record->status == DELEGATE_TASK_QUEUED) {
            strscpy(view->status, "queued", sizeof(view->status));
            queued_count++;
            any_queued = true;
        } else {
            strscpy(view->status, "running", sizeof(view->status));
            any_running = true;
            running_count++;
        }

        if (record->blocker_kind[0] || record->blocker_text[0]) {
            any_blocked = true;
            blocked_count++;
            coordinator_blocker_kind = record->blocker_kind;
            coordinator_blocker_text = record->blocker_text;
        }
    }

    coordinator->completed_count = completed_count;
    coordinator->running_count = running_count;
    coordinator->queued_count = queued_count;
    coordinator->blocked_count = blocked_count;
    coordinator->failed_count = failed_count;
    coordinator->effective_output_count = effective_output_count;
    if (any_running) {
        strscpy(coordinator->status, "running", sizeof(coordinator->status));
    } else if (any_queued) {
        strscpy(coordinator->status, "queued", sizeof(coordinator->status));
    } else if (any_failed) {
        strscpy(coordinator->status, "failed", sizeof(coordinator->status));
    } else if (completed_count > 0 && effective_output_count == 0) {
        strscpy(coordinator->status, "failed", sizeof(coordinator->status));
        any_blocked = true;
        coordinator_blocker_kind = "empty_output";
        coordinator_blocker_text = "all subagents completed without effective output";
    } else {
        strscpy(coordinator->status, "done", sizeof(coordinator->status));
    }
    strscpy(coordinator->blocker_kind, any_blocked ? coordinator_blocker_kind : "",
            sizeof(coordinator->blocker_kind));
    strscpy(coordinator->blocker_text, any_blocked ? coordinator_blocker_text : "",
            sizeof(coordinator->blocker_text));
}
