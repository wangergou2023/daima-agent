/* Delegate coordinator wake manager. Inspired by background task parent wake
 * managers: decouple store change detection from websocket delivery and keep
 * retryable pending wake state locally. */
#include "delegate_parent_wake.h"
#include "delegate_state_json.h"
#include "delegate_session_json.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cjson.h"
#include "drivers/channel/gateway/ws_client.h"
#include "drivers/channel/gateway/ws_server.h"
#include "drivers/tool/tool_delegate_result_json.h"
#include "bus.h"
#include "linux/kernel.h"
#include "linux/mutex.h"
#include "linux/printk.h"
#include "kernel/turn/turn_context.h"
#include "text.h"
#define DELEGATE_PARENT_WAKE_PENDING_MAX DELEGATE_COORDINATOR_STORE_MAX
#define DELEGATE_PARENT_WAKE_RETRY_MS 1000L
#define DELEGATE_PARENT_WAKE_ACTIVITY_WINDOW_MS 2000L

typedef struct {
    bool used;
    delegate_coordinator_record_t record;
    long retry_after_ms;
    bool resume_pending;
    long resume_deferred_at_ms;
} delegate_parent_wake_entry_t;

typedef struct {
    bool used;
    char chat_id[64];
    long last_activity_ms;
} delegate_parent_activity_entry_t;

static struct mutex s_wake_mutex;
static bool s_wake_mutex_inited = false;
static delegate_parent_wake_entry_t s_pending[DELEGATE_PARENT_WAKE_PENDING_MAX];
static delegate_parent_activity_entry_t s_parent_activity[DELEGATE_COORDINATOR_STORE_MAX];
static long s_parent_activity_window_ms = DELEGATE_PARENT_WAKE_ACTIVITY_WINDOW_MS;

static delegate_parent_wake_sender_fn_t s_status_sender = ws_server_send_coordinator_status;
static delegate_parent_wake_sender_fn_t s_output_sender = ws_server_send_coordinator_output;
static delegate_parent_wake_sender_fn_t s_done_sender = ws_server_send_coordinator_done;
static delegate_parent_wake_sender_fn_t s_session_sender = ws_server_send_subagent_session;
static delegate_parent_wake_subagent_sender_fn_t s_subagent_sender = ws_server_send_subagent_event;

static void clear_pending_entry(int idx);
static void retain_pending_resume_locked(int idx,
                                         const delegate_coordinator_record_t *record,
                                         long deferred_at_ms);
static void defer_pending_resume_locked(int idx,
                                        const delegate_coordinator_record_t *record,
                                        long retry_after_ms,
                                        long deferred_at_ms);
static void drop_pending_resume_locked(int idx,
                                       const delegate_coordinator_record_t *record);
static err_t dispatch_coordinator_snapshot(const delegate_coordinator_record_t *record);
static err_t dispatch_terminal_completion(const delegate_coordinator_record_t *record);
static err_t dispatch_visible_coordinator_update(const delegate_coordinator_record_t *record);
static bool parent_chat_has_recent_activity_locked(const char *chat_id, long now_ms);
static bool parent_chat_has_pending_request(const char *chat_id);
static bool parent_chat_consumed_delegate_resume(const char *chat_id,
                                                 const char *coordinator_id,
                                                 unsigned long visible_revision);
static void render_subagent_visible_output(const char *raw_output,
                                           char *visible_output,
                                           size_t visible_output_size);
static void replay_visible_coordinator(const delegate_coordinator_record_t *record);

static long monotonic_ms_now(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (long)(ts.tv_sec * 1000L + ts.tv_nsec / 1000000L);
}

static void ensure_wake_init(void)
{
    if (!s_wake_mutex_inited) {
        mutex_init(&s_wake_mutex);
        s_wake_mutex_inited = true;
    }
}

static bool coordinator_chat_is_web_visible(const char *chat_id)
{
    return chat_id &&
           chat_id[0] &&
           strncmp(chat_id, "delegate_sync_", 14) != 0;
}

static bool coordinator_chat_has_live_ws_client(const char *chat_id)
{
    return coordinator_chat_is_web_visible(chat_id) &&
           ws_client_session_has_chat_id(chat_id);
}

static bool coordinator_is_terminal(const delegate_coordinator_record_t *record)
{
    return record &&
           (strcmp(record->status, "done") == 0 || strcmp(record->status, "failed") == 0);
}

static bool coordinator_terminal_resume_requires_reply(const delegate_coordinator_record_t *record)
{
    return record &&
           coordinator_is_terminal(record) &&
           strcmp(record->status, "failed") == 0;
}

static bool coordinator_has_effective_output(const delegate_coordinator_record_t *record)
{
    return record && record->effective_output_count > 0;
}

static bool coordinator_has_resume_pending(const delegate_coordinator_record_t *record)
{
    return record &&
           coordinator_is_terminal(record) &&
           record->completion_notified &&
           !record->parent_resume_enqueued;
}

static bool should_defer_terminal_parent_resume(const delegate_coordinator_record_t *record,
                                                long now_ms,
                                                long resume_deferred_at_ms,
                                                long *retry_after_ms_out,
                                                bool *drop_resume_out)
{
    bool requires_reply;

    if (retry_after_ms_out) {
        *retry_after_ms_out = 0;
    }
    if (drop_resume_out) {
        *drop_resume_out = false;
    }
    if (!record || !record->chat_id[0]) {
        return false;
    }

    if (parent_chat_has_pending_request(record->chat_id)) {
        if (retry_after_ms_out) {
            *retry_after_ms_out = now_ms + DELEGATE_PARENT_WAKE_RETRY_MS;
        }
        return true;
    }

    mutex_lock(&s_wake_mutex);
    requires_reply = coordinator_terminal_resume_requires_reply(record);
    if (!requires_reply &&
        parent_chat_consumed_delegate_resume(record->chat_id,
                                             record->coordinator_id,
                                             record->visible_revision)) {
        if (drop_resume_out) {
            *drop_resume_out = true;
        }
        mutex_unlock(&s_wake_mutex);
        return true;
    }
    if (parent_chat_has_recent_activity_locked(record->chat_id, now_ms)) {
        (void)resume_deferred_at_ms;
        if (retry_after_ms_out) {
            *retry_after_ms_out = now_ms + s_parent_activity_window_ms;
        }
        mutex_unlock(&s_wake_mutex);
        return true;
    }
    mutex_unlock(&s_wake_mutex);
    return false;
}

static char *build_subagent_session_json(const delegate_coordinator_record_t *record,
                                         const delegate_coordinator_agent_view_t *agent);

static int find_pending_index(const char *coordinator_id)
{
    if (!coordinator_id || !coordinator_id[0]) {
        return -1;
    }
    for (int i = 0; i < DELEGATE_PARENT_WAKE_PENDING_MAX; i++) {
        if (s_pending[i].used &&
            strcmp(s_pending[i].record.coordinator_id, coordinator_id) == 0) {
            return i;
        }
    }
    return -1;
}

static void compact_duplicate_pending_locked(const char *coordinator_id, int keep_idx)
{
    if (!coordinator_id || !coordinator_id[0]) {
        return;
    }
    for (int i = 0; i < DELEGATE_PARENT_WAKE_PENDING_MAX; i++) {
        if (i == keep_idx || !s_pending[i].used) {
            continue;
        }
        if (strcmp(s_pending[i].record.coordinator_id, coordinator_id) != 0) {
            continue;
        }
        clear_pending_entry(i);
    }
}

static bool pending_entry_is_newer(const delegate_parent_wake_entry_t *left,
                                   const delegate_parent_wake_entry_t *right)
{
    if (!left || !right) {
        return false;
    }
    if (left->resume_pending != right->resume_pending) {
        return left->resume_pending;
    }
    if (left->record.visible_revision != right->record.visible_revision) {
        return left->record.visible_revision > right->record.visible_revision;
    }
    if (left->record.last_sent_revision != right->record.last_sent_revision) {
        return left->record.last_sent_revision > right->record.last_sent_revision;
    }
    return left->retry_after_ms >= right->retry_after_ms;
}

static void dedupe_pending_entries_locked(void)
{
    for (int i = 0; i < DELEGATE_PARENT_WAKE_PENDING_MAX; i++) {
        if (!s_pending[i].used || !s_pending[i].record.coordinator_id[0]) {
            continue;
        }
        for (int j = i + 1; j < DELEGATE_PARENT_WAKE_PENDING_MAX; j++) {
            if (!s_pending[j].used || !s_pending[j].record.coordinator_id[0]) {
                continue;
            }
            if (strcmp(s_pending[i].record.coordinator_id,
                       s_pending[j].record.coordinator_id) != 0) {
                continue;
            }
            if (pending_entry_is_newer(&s_pending[j], &s_pending[i])) {
                s_pending[i] = s_pending[j];
            }
            clear_pending_entry(j);
        }
    }
}

static int allocate_pending_index(void)
{
    for (int i = 0; i < DELEGATE_PARENT_WAKE_PENDING_MAX; i++) {
        if (!s_pending[i].used) {
            return i;
        }
    }
    return 0;
}

static int find_parent_activity_index(const char *chat_id)
{
    if (!chat_id || !chat_id[0]) {
        return -1;
    }
    for (int i = 0; i < DELEGATE_COORDINATOR_STORE_MAX; i++) {
        if (s_parent_activity[i].used &&
            strcmp(s_parent_activity[i].chat_id, chat_id) == 0) {
            return i;
        }
    }
    return -1;
}

static int allocate_parent_activity_index(void)
{
    for (int i = 0; i < DELEGATE_COORDINATOR_STORE_MAX; i++) {
        if (!s_parent_activity[i].used) {
            return i;
        }
    }
    return 0;
}

static void record_parent_activity_locked(const char *chat_id)
{
    int idx;

    if (!chat_id || !chat_id[0]) {
        return;
    }
    idx = find_parent_activity_index(chat_id);
    if (idx < 0) {
        idx = allocate_parent_activity_index();
        memset(&s_parent_activity[idx], 0, sizeof(s_parent_activity[idx]));
        s_parent_activity[idx].used = true;
        strscpy(s_parent_activity[idx].chat_id, chat_id, sizeof(s_parent_activity[idx].chat_id));
    }
    s_parent_activity[idx].last_activity_ms = monotonic_ms_now();
}

static bool parent_chat_has_recent_activity_locked(const char *chat_id, long now_ms)
{
    int idx;
    long elapsed_ms;

    if (!chat_id || !chat_id[0] || s_parent_activity_window_ms <= 0) {
        return false;
    }
    idx = find_parent_activity_index(chat_id);
    if (idx < 0 || !s_parent_activity[idx].last_activity_ms) {
        return false;
    }
    elapsed_ms = now_ms - s_parent_activity[idx].last_activity_ms;
    if (elapsed_ms < 0) {
        elapsed_ms = 0;
    }
    if (elapsed_ms <= s_parent_activity_window_ms) {
        return true;
    }
    memset(&s_parent_activity[idx], 0, sizeof(s_parent_activity[idx]));
    return false;
}

static bool parent_chat_has_pending_request(const char *chat_id)
{
    struct turn_snapshot snap;
    bool has_pending = false;

    if (!chat_id || !chat_id[0]) {
        return false;
    }

    memset(&snap, 0, sizeof(snap));
    if (!turn_context_load_copy(chat_id, &snap)) {
        return false;
    }
    has_pending = snap.pending_request_type[0] != '\0' &&
                  snap.pending_request_id[0] != '\0';
    turn_context_snapshot_cleanup(&snap);
    return has_pending;
}

static bool parent_chat_consumed_delegate_resume(const char *chat_id,
                                                 const char *coordinator_id,
                                                 unsigned long visible_revision)
{
    return turn_context_has_delegate_resume_consumed(chat_id,
                                                     coordinator_id,
                                                     visible_revision);
}

static void render_subagent_visible_output(const char *raw_output,
                                           char *visible_output,
                                           size_t visible_output_size)
{
    if (!visible_output || !visible_output_size) {
        return;
    }
    visible_output[0] = '\0';
    if (!raw_output || !raw_output[0]) {
        return;
    }
    if (tool_delegate_parse_result_json_rendered(raw_output, visible_output, visible_output_size)) {
        return;
    }
    text_shorten(raw_output, visible_output, visible_output_size, 220);
}

static void render_subagent_visible_output_from_task(const delegate_task_record_t *task_snapshot,
                                                     char *visible_output,
                                                     size_t visible_output_size)
{
    char preferred[1024];

    if (!visible_output || !visible_output_size) {
        return;
    }
    visible_output[0] = '\0';
    if (!task_snapshot) {
        return;
    }

    memset(preferred, 0, sizeof(preferred));
    if (delegate_child_session_preferred_visible_text(task_snapshot,
                                                      preferred,
                                                      sizeof(preferred)) &&
        preferred[0]) {
        text_shorten(preferred, visible_output, visible_output_size, 220);
        return;
    }

    render_subagent_visible_output(task_snapshot->output,
                                   visible_output,
                                   visible_output_size);
}

static void send_subagent_progress_events(const delegate_coordinator_record_t *record)
{
    if (!record || !record->chat_id[0] || !s_subagent_sender) {
        return;
    }

    for (int i = 0; i < record->agent_count; i++) {
        const delegate_coordinator_agent_view_t *agent = &record->agents[i];
        delegate_task_record_t task_snapshot;
        char detail[192];
        const char *output_text = "";
        char visible_output[512];

        memset(&task_snapshot, 0, sizeof(task_snapshot));
        visible_output[0] = '\0';
        if (delegate_task_store_snapshot(agent->task_id, &task_snapshot) == 0) {
            output_text = task_snapshot.output;
            render_subagent_visible_output_from_task(&task_snapshot,
                                                     visible_output,
                                                     sizeof(visible_output));
        }

        detail[0] = '\0';
        if (agent->model[0]) {
            snprintf(detail, sizeof(detail), "model=%s · elapsed_ms=%ld",
                     agent->model, agent->elapsed_ms);
        } else {
            snprintf(detail, sizeof(detail), "elapsed_ms=%ld", agent->elapsed_ms);
        }
        if (agent->write_approved && strlen(detail) + 18 < sizeof(detail)) {
            strlcat(detail, " · write_ready", sizeof(detail));
        }
        if (agent->target_files[0] && strlen(detail) + 32 < sizeof(detail)) {
            strlcat(detail, " · target_files=", sizeof(detail));
            strlcat(detail, agent->target_files, sizeof(detail));
        }
        if (agent->scope_path[0] && strlen(detail) + 32 < sizeof(detail)) {
            strlcat(detail, " · scope=", sizeof(detail));
            strlcat(detail, agent->scope_path, sizeof(detail));
        }
        if (agent->analysis_focus[0] && strlen(detail) + 24 < sizeof(detail)) {
            strlcat(detail, " · focus=", sizeof(detail));
            strlcat(detail, agent->analysis_focus, sizeof(detail));
        }

        s_subagent_sender(record->chat_id,
                          "subagent_progress",
                          agent->task_id,
                          agent->task_key,
                          agent->session_id,
                          record->coordinator_id,
                          record->visible_revision,
                          agent->subagent_type,
                          agent->status,
                          agent->description,
                          detail,
                          output_text,
                          visible_output,
                          agent->target_files,
                          agent->scope_path,
                          agent->scope_kind,
                          agent->analysis_focus,
                          agent->blocker_kind,
                          agent->blocker_text,
                          "task");

        if (s_session_sender) {
            char *session_json = build_subagent_session_json(record, agent);
            if (session_json) {
                (void)s_session_sender(record->chat_id, session_json);
                free(session_json);
            }
        }

        if (strcmp(agent->status, "done") == 0 || strcmp(agent->status, "failed") == 0) {
            char done_detail[192];
            done_detail[0] = '\0';
            if (agent->model[0]) {
                snprintf(done_detail, sizeof(done_detail), "model=%s · elapsed_ms=%ld",
                         agent->model, agent->elapsed_ms);
            } else {
                snprintf(done_detail, sizeof(done_detail), "elapsed_ms=%ld", agent->elapsed_ms);
            }
            s_subagent_sender(record->chat_id,
                              "subagent_done",
                              agent->task_id,
                              agent->task_key,
                              agent->session_id,
                              record->coordinator_id,
                              record->visible_revision,
                              agent->subagent_type,
                              agent->status,
                              agent->description,
                              done_detail,
                              output_text,
                              visible_output,
                              agent->target_files,
                              agent->scope_path,
                              agent->scope_kind,
                              agent->analysis_focus,
                              agent->blocker_kind,
                              agent->blocker_text,
                              "task");
        }

        if (agent->blocker_kind[0] || agent->blocker_text[0]) {
            s_subagent_sender(record->chat_id,
                              "subagent_blocked",
                              agent->task_id,
                              agent->task_key,
                              agent->session_id,
                              record->coordinator_id,
                              record->visible_revision,
                              agent->subagent_type,
                              agent->status,
                              agent->description,
                              agent->blocker_text,
                              output_text,
                              visible_output,
                              agent->target_files,
                              agent->scope_path,
                              agent->scope_kind,
                              agent->analysis_focus,
                              agent->blocker_kind,
                              agent->blocker_text,
                              "task");
        }
    }

    if (record->blocker_kind[0] || record->blocker_text[0]) {
        s_subagent_sender(record->chat_id,
                          "subagent_blocked",
                          "",
                          "",
                          "",
                          record->coordinator_id,
                          record->visible_revision,
                          "coordinator",
                          record->status,
                          "Coordinator wake",
                          record->blocker_text,
                          "",
                          "",
                          "",
                          "",
                          "",
                          "",
                          record->blocker_kind,
                          record->blocker_text,
                          "coordinator");
    }
}

static char *build_completion_json(const delegate_coordinator_record_t *record)
{
    return delegate_coordinator_completion_json_build(record);
}

static char *build_subagent_session_json(const delegate_coordinator_record_t *record,
                                         const delegate_coordinator_agent_view_t *agent)
{
    return delegate_subagent_session_payload_json_build(record, agent);
}

static char *build_parent_resume_content(const delegate_coordinator_record_t *record)
{
    char *completion_json;
    const char *fmt =
        "Delegate coordinator completed. Summarize the finished subagent outputs for the user directly.\n"
        "Do not relaunch delegation unless the existing outputs are clearly insufficient.\n"
        "Prefer a merged final answer grounded in the completed agent outputs below.\n\n"
        "Coordinator snapshot:\n%s";
    size_t need;
    char *buf;

    if (!record) {
        return NULL;
    }

    completion_json = build_completion_json(record);
    if (!completion_json) {
        return NULL;
    }

    need = snprintf(NULL, 0, fmt, completion_json) + 1;
    buf = malloc(need);
    if (!buf) {
        free(completion_json);
        return NULL;
    }
    snprintf(buf, need, fmt, completion_json);
    free(completion_json);
    return buf;
}

static err_t enqueue_parent_resume_message(const delegate_coordinator_record_t *record)
{
    struct message msg;
    char *content;

    if (!record || !record->chat_id[0]) {
        return ERR_INVALID_ARG;
    }

    content = build_parent_resume_content(record);
    if (!content) {
        return ERR_NO_MEM;
    }

    memset(&msg, 0, sizeof(msg));
    strscpy(msg.channel, CHAN_WEBSOCKET, sizeof(msg.channel));
    strscpy(msg.chat_id, record->chat_id, sizeof(msg.chat_id));
    strscpy(msg.source, MSG_SOURCE_DELEGATE, sizeof(msg.source));
    msg.content = content;
    msg.intent = INTENT_OPEN;

    return message_bus_push_inbound(&msg);
}

static void clear_pending_entry(int idx)
{
    if (idx < 0 || idx >= DELEGATE_PARENT_WAKE_PENDING_MAX) {
        return;
    }
    memset(&s_pending[idx], 0, sizeof(s_pending[idx]));
}

static void retain_pending_resume_locked(int idx,
                                         const delegate_coordinator_record_t *record,
                                         long deferred_at_ms)
{
    if (idx < 0 || idx >= DELEGATE_PARENT_WAKE_PENDING_MAX ||
        !s_pending[idx].used || !record) {
        return;
    }
    s_pending[idx].record = *record;
    s_pending[idx].resume_pending = true;
    s_pending[idx].retry_after_ms = 0;
    if (s_pending[idx].resume_deferred_at_ms <= 0) {
        s_pending[idx].resume_deferred_at_ms = deferred_at_ms;
    }
}

static void defer_pending_resume_locked(int idx,
                                        const delegate_coordinator_record_t *record,
                                        long retry_after_ms,
                                        long deferred_at_ms)
{
    if (idx < 0 || idx >= DELEGATE_PARENT_WAKE_PENDING_MAX ||
        !s_pending[idx].used || !record) {
        return;
    }
    s_pending[idx].record = *record;
    s_pending[idx].resume_pending = true;
    s_pending[idx].retry_after_ms = retry_after_ms;
    if (s_pending[idx].resume_deferred_at_ms <= 0) {
        s_pending[idx].resume_deferred_at_ms = deferred_at_ms;
    }
}

static void drop_pending_resume_locked(int idx,
                                       const delegate_coordinator_record_t *record)
{
    if (idx < 0 || idx >= DELEGATE_PARENT_WAKE_PENDING_MAX || !record) {
        return;
    }
    delegate_task_store_mark_wake_completed(record->coordinator_id);
    if (s_pending[idx].used) {
        s_pending[idx].record = *record;
        s_pending[idx].resume_pending = false;
        s_pending[idx].resume_deferred_at_ms = 0;
    }
    clear_pending_entry(idx);
}

static void upsert_pending_locked(const delegate_coordinator_record_t *record)
{
    int idx = find_pending_index(record->coordinator_id);
    if (idx < 0) {
        idx = allocate_pending_index();
    }
    compact_duplicate_pending_locked(record->coordinator_id, idx);
    if (s_pending[idx].used &&
        s_pending[idx].record.visible_revision == record->visible_revision &&
        s_pending[idx].record.last_sent_revision == record->last_sent_revision &&
        s_pending[idx].retry_after_ms > 0) {
        return;
    }
    s_pending[idx].used = true;
    s_pending[idx].record = *record;
    s_pending[idx].retry_after_ms = 0;
    s_pending[idx].resume_pending = coordinator_has_resume_pending(record);
    if (!s_pending[idx].resume_pending) {
        s_pending[idx].resume_deferred_at_ms = 0;
    }
}

static void drain_store_changes_into_pending(void)
{
    delegate_coordinator_record_t changed[DELEGATE_COORDINATOR_STORE_MAX];
    memset(changed, 0, sizeof(changed));
    if (!delegate_task_store_drain_changed_coordinators(changed,
                                                        sizeof(changed) / sizeof(changed[0]))) {
        return;
    }

    mutex_lock(&s_wake_mutex);
    for (size_t i = 0; i < sizeof(changed) / sizeof(changed[0]); i++) {
        if (!changed[i].coordinator_id[0]) {
            continue;
        }
        upsert_pending_locked(&changed[i]);
        delegate_task_store_mark_wake_pending(changed[i].coordinator_id);
    }
    mutex_unlock(&s_wake_mutex);
}

static void retry_pending_entry(int idx, err_t err)
{
    s_pending[idx].retry_after_ms = monotonic_ms_now() + DELEGATE_PARENT_WAKE_RETRY_MS;
    delegate_task_store_mark_wake_retry(s_pending[idx].record.coordinator_id, err);
    pr_warn("delegate_parent_wake: retry queued for coordinator=%s err=%s",
            s_pending[idx].record.coordinator_id, err_name(err));
}

static void handle_non_web_terminal(const delegate_coordinator_record_t *record)
{
    if (coordinator_is_terminal(record) && !record->completion_notified) {
        delegate_task_store_mark_completion_notified(record->coordinator_id);
        delegate_task_store_mark_wake_completed(record->coordinator_id);
    }
}

static void finish_terminal_missing_parent_client(const delegate_coordinator_record_t *record,
                                                  int idx)
{
    delegate_coordinator_record_t drained[DELEGATE_COORDINATOR_STORE_MAX];

    if (!record || !coordinator_is_terminal(record)) {
        return;
    }

    delegate_task_store_mark_completion_notified(record->coordinator_id);
    delegate_task_store_mark_wake_completed(record->coordinator_id);
    memset(drained, 0, sizeof(drained));
    (void)delegate_task_store_drain_changed_coordinators(drained,
                                                         sizeof(drained) / sizeof(drained[0]));
    mutex_lock(&s_wake_mutex);
    clear_pending_entry(idx);
    mutex_unlock(&s_wake_mutex);
}

static err_t complete_terminal_via_parent_resume(const delegate_coordinator_record_t *record)
{
    if (!record || !coordinator_is_terminal(record)) {
        return ERR_INVALID_ARG;
    }

    if (!record->completion_notified) {
        delegate_task_store_mark_completion_notified(record->coordinator_id);
    }

    err_t err = enqueue_parent_resume_message(record);
    if (err != 0) {
        return err;
    }

    delegate_task_store_mark_parent_resume_enqueued(record->coordinator_id);
    delegate_task_store_mark_wake_completed(record->coordinator_id);
    pr_info("delegate_parent_wake: parent resume enqueued without live websocket delivery coordinator=%s chat=%s",
            record->coordinator_id,
            record->chat_id);
    return 0;
}

static err_t dispatch_coordinator_snapshot(const delegate_coordinator_record_t *record)
{
    char *snapshot_json;
    err_t err;

    if (!record) {
        return ERR_INVALID_ARG;
    }

    snapshot_json = delegate_coordinator_snapshot_json_build(record, false);
    if (!snapshot_json) {
        return ERR_NO_MEM;
    }

    err = s_status_sender(record->chat_id, snapshot_json);
    if (err == 0) {
        err = s_output_sender(record->chat_id, snapshot_json);
    }
    free(snapshot_json);
    return err;
}

static err_t dispatch_terminal_completion(const delegate_coordinator_record_t *record)
{
    char *done_json;
    err_t err;

    if (!record) {
        return ERR_INVALID_ARG;
    }

    done_json = build_completion_json(record);
    if (!done_json) {
        return ERR_NO_MEM;
    }
    err = s_done_sender(record->chat_id, done_json);
    free(done_json);
    return err;
}

static err_t dispatch_visible_coordinator_update(const delegate_coordinator_record_t *record)
{
    err_t err;

    if (!record) {
        return ERR_INVALID_ARG;
    }

    err = dispatch_coordinator_snapshot(record);
    if (err != 0) {
        return err;
    }

    send_subagent_progress_events(record);
    if (record->visible_revision != 0) {
        delegate_task_store_mark_visible_revision_sent(record->coordinator_id, record->visible_revision);
    }
    return 0;
}

static void flush_pending_snapshot(const delegate_parent_wake_entry_t *snapshot)
{
    delegate_coordinator_record_t record;
    delegate_coordinator_record_t latest_record;
    long retry_after_ms;
    long now_ms = monotonic_ms_now();
    bool terminal = false;
    bool resume_pending = false;
    long resume_deferred_at_ms = 0;
    int idx = -1;

    if (!snapshot || !snapshot->used || !snapshot->record.coordinator_id[0]) {
        return;
    }
    record = snapshot->record;
    retry_after_ms = snapshot->retry_after_ms;
    resume_pending = snapshot->resume_pending;
    resume_deferred_at_ms = snapshot->resume_deferred_at_ms;

    memset(&latest_record, 0, sizeof(latest_record));
    if (record.coordinator_id[0] &&
        delegate_task_store_snapshot_coordinator(record.coordinator_id, &latest_record) == 0 &&
        latest_record.coordinator_id[0]) {
        record = latest_record;
    }
    mutex_lock(&s_wake_mutex);
    idx = find_pending_index(record.coordinator_id);
    mutex_unlock(&s_wake_mutex);
    if (idx < 0) {
        return;
    }

    if (retry_after_ms > 0 && now_ms < retry_after_ms) {
        return;
    }

    if (!record.parent_response_sent) {
        return;
    }

    terminal = coordinator_is_terminal(&record);
    resume_pending = resume_pending || coordinator_has_resume_pending(&record);

    if (record.visible_revision != 0 &&
        record.last_sent_revision == record.visible_revision &&
        !(terminal && !record.completion_notified) &&
        !resume_pending) {
        mutex_lock(&s_wake_mutex);
        clear_pending_entry(idx);
        mutex_unlock(&s_wake_mutex);
        return;
    }

    if (!coordinator_chat_is_web_visible(record.chat_id)) {
        handle_non_web_terminal(&record);
        mutex_lock(&s_wake_mutex);
        clear_pending_entry(idx);
        mutex_unlock(&s_wake_mutex);
        return;
    }

    if (!coordinator_chat_has_live_ws_client(record.chat_id) &&
        !terminal &&
        !resume_pending) {
        delegate_task_store_mark_wake_completed(record.coordinator_id);
        pr_info("delegate_parent_wake: skip live dispatch without websocket client coordinator=%s chat=%s",
                record.coordinator_id,
                record.chat_id);
        mutex_lock(&s_wake_mutex);
        clear_pending_entry(idx);
        mutex_unlock(&s_wake_mutex);
        return;
    }

    if (!resume_pending) {
        delegate_task_store_mark_wake_dispatched(record.coordinator_id);
        memset(&latest_record, 0, sizeof(latest_record));
        if (delegate_task_store_snapshot_coordinator(record.coordinator_id, &latest_record) == 0 &&
            latest_record.coordinator_id[0]) {
            record = latest_record;
        }
        terminal = coordinator_is_terminal(&record);

        err_t err = dispatch_visible_coordinator_update(&record);

        if (err == ERR_NOT_FOUND) {
            if (terminal) {
                err = complete_terminal_via_parent_resume(&record);
                if (err == 0) {
                    mutex_lock(&s_wake_mutex);
                    clear_pending_entry(idx);
                    mutex_unlock(&s_wake_mutex);
                    return;
                }
            }
            delegate_task_store_mark_wake_completed(record.coordinator_id);
            pr_info("delegate_parent_wake: no live websocket client for coordinator=%s chat=%s, rely on session snapshot/reconnect recovery",
                    record.coordinator_id,
                    record.chat_id);
            mutex_lock(&s_wake_mutex);
            clear_pending_entry(idx);
            mutex_unlock(&s_wake_mutex);
            return;
        }
        if (err != 0) {
            if (terminal) {
                err_t resume_err = complete_terminal_via_parent_resume(&record);
                if (resume_err == 0) {
                    mutex_lock(&s_wake_mutex);
                    clear_pending_entry(idx);
                    mutex_unlock(&s_wake_mutex);
                    return;
                }
            }
            mutex_lock(&s_wake_mutex);
            retry_pending_entry(idx, err);
            mutex_unlock(&s_wake_mutex);
            return;
        }

        if (terminal && !record.completion_notified) {
            if (!coordinator_has_effective_output(&record) &&
                strcmp(record.status, "done") == 0) {
                mutex_lock(&s_wake_mutex);
                retry_pending_entry(idx, ERR_TIMEOUT);
                mutex_unlock(&s_wake_mutex);
                return;
            }
            err = dispatch_terminal_completion(&record);
            if (err == ERR_NOT_FOUND) {
                err = complete_terminal_via_parent_resume(&record);
                if (err == 0) {
                    mutex_lock(&s_wake_mutex);
                    clear_pending_entry(idx);
                    mutex_unlock(&s_wake_mutex);
                    return;
                }
            }
            if (err != 0) {
                err_t resume_err = complete_terminal_via_parent_resume(&record);
                if (resume_err == 0) {
                    mutex_lock(&s_wake_mutex);
                    clear_pending_entry(idx);
                    mutex_unlock(&s_wake_mutex);
                    return;
                }
                mutex_lock(&s_wake_mutex);
                retry_pending_entry(idx, err);
                mutex_unlock(&s_wake_mutex);
                return;
            }
            delegate_task_store_mark_completion_notified(record.coordinator_id);
            record.completion_notified = true;
        }
        resume_pending = coordinator_has_resume_pending(&record);
        if (resume_pending) {
            mutex_lock(&s_wake_mutex);
            retain_pending_resume_locked(idx, &record, monotonic_ms_now());
            mutex_unlock(&s_wake_mutex);
        }
    }

    if (resume_pending) {
        long retry_after_ms = 0;
        bool drop_resume = false;
        if (should_defer_terminal_parent_resume(&record,
                                                now_ms,
                                                resume_deferred_at_ms,
                                                &retry_after_ms,
                                                &drop_resume)) {
            if (drop_resume) {
                mutex_lock(&s_wake_mutex);
                drop_pending_resume_locked(idx, &record);
                pr_info("delegate_parent_wake: dropped retained parent resume after parent activity coordinator=%s chat=%s",
                        record.coordinator_id,
                        record.chat_id);
                mutex_unlock(&s_wake_mutex);
                return;
            }
            mutex_lock(&s_wake_mutex);
            defer_pending_resume_locked(idx,
                                        &record,
                                        retry_after_ms,
                                        now_ms);
            mutex_unlock(&s_wake_mutex);
            if (retry_after_ms == now_ms + DELEGATE_PARENT_WAKE_RETRY_MS) {
                pr_info("delegate_parent_wake: deferred parent resume because parent has pending interactive request coordinator=%s chat=%s",
                        record.coordinator_id,
                        record.chat_id);
            } else {
                pr_info("delegate_parent_wake: deferred parent resume coordinator=%s chat=%s retry_after_ms=%ld",
                        record.coordinator_id,
                        record.chat_id,
                        retry_after_ms);
            }
            return;
        }

        pr_info("delegate_parent_wake: enqueue parent resume coordinator=%s chat=%s",
                record.coordinator_id,
                record.chat_id);
        err_t err = enqueue_parent_resume_message(&record);
        if (err != 0) {
            mutex_lock(&s_wake_mutex);
            retry_pending_entry(idx, err);
            mutex_unlock(&s_wake_mutex);
            return;
        }
        delegate_task_store_mark_parent_resume_enqueued(record.coordinator_id);
        record.parent_resume_enqueued = true;
        delegate_task_store_mark_wake_completed(record.coordinator_id);
        pr_info("delegate_parent_wake: parent resume enqueued coordinator=%s chat=%s",
                record.coordinator_id,
                record.chat_id);
    }

    mutex_lock(&s_wake_mutex);
    clear_pending_entry(idx);
    mutex_unlock(&s_wake_mutex);
}

err_t delegate_parent_wake_init(void)
{
    ensure_wake_init();
    return 0;
}

void delegate_parent_wake_poll(void)
{
    delegate_parent_wake_entry_t snapshot[DELEGATE_PARENT_WAKE_PENDING_MAX];

    ensure_wake_init();
    drain_store_changes_into_pending();
    mutex_lock(&s_wake_mutex);
    dedupe_pending_entries_locked();
    memcpy(snapshot, s_pending, sizeof(snapshot));
    mutex_unlock(&s_wake_mutex);
    for (int i = 0; i < DELEGATE_PARENT_WAKE_PENDING_MAX; i++) {
        flush_pending_snapshot(&snapshot[i]);
    }
}

static void replay_visible_coordinator(const delegate_coordinator_record_t *record)
{
    if (!record || !record->chat_id[0]) {
        return;
    }
    if (!coordinator_chat_is_web_visible(record->chat_id)) {
        return;
    }
    if (!coordinator_chat_has_live_ws_client(record->chat_id)) {
        return;
    }

    (void)dispatch_visible_coordinator_update(record);
    if (coordinator_is_terminal(record) && record->completion_notified) {
        (void)dispatch_terminal_completion(record);
    }
}

void delegate_parent_wake_replay_chat(const char *chat_id)
{
    delegate_parent_registry_view_t parent_view;

    if (!chat_id || !chat_id[0]) {
        return;
    }

    memset(&parent_view, 0, sizeof(parent_view));
    if (delegate_task_store_snapshot_parent(chat_id, &parent_view) != 0) {
        return;
    }

    for (int i = 0; i < parent_view.coordinator_count; i++) {
        delegate_coordinator_record_t snapshot;
        const delegate_parent_coordinator_list_item_t *summary = &parent_view.coordinators[i];

        if (!summary->coordinator_id[0]) {
            continue;
        }
        memset(&snapshot, 0, sizeof(snapshot));
        if (delegate_task_store_snapshot_coordinator(summary->coordinator_id, &snapshot) != 0) {
            continue;
        }
        replay_visible_coordinator(&snapshot);
    }
}

bool delegate_parent_wake_is_idle(void)
{
    bool idle = true;

    ensure_wake_init();
    mutex_lock(&s_wake_mutex);
    for (int i = 0; i < DELEGATE_PARENT_WAKE_PENDING_MAX; i++) {
        if (!s_pending[i].used) {
            continue;
        }
        idle = false;
        break;
    }
    mutex_unlock(&s_wake_mutex);
    return idle;
}

void delegate_parent_wake_record_parent_activity(const char *chat_id)
{
    ensure_wake_init();
    mutex_lock(&s_wake_mutex);
    record_parent_activity_locked(chat_id);
    mutex_unlock(&s_wake_mutex);
}

void delegate_parent_wake_reset_for_test(void)
{
    ensure_wake_init();
    mutex_lock(&s_wake_mutex);
    memset(s_pending, 0, sizeof(s_pending));
    memset(s_parent_activity, 0, sizeof(s_parent_activity));
    s_parent_activity_window_ms = DELEGATE_PARENT_WAKE_ACTIVITY_WINDOW_MS;
    s_status_sender = ws_server_send_coordinator_status;
    s_output_sender = ws_server_send_coordinator_output;
    s_done_sender = ws_server_send_coordinator_done;
    s_session_sender = ws_server_send_subagent_session;
    s_subagent_sender = ws_server_send_subagent_event;
    mutex_unlock(&s_wake_mutex);
}

void delegate_parent_wake_set_sender_overrides_for_test(delegate_parent_wake_sender_fn_t status_sender,
                                                        delegate_parent_wake_sender_fn_t output_sender,
                                                        delegate_parent_wake_sender_fn_t done_sender,
                                                        delegate_parent_wake_subagent_sender_fn_t subagent_sender)
{
    ensure_wake_init();
    mutex_lock(&s_wake_mutex);
    s_status_sender = status_sender ? status_sender : ws_server_send_coordinator_status;
    s_output_sender = output_sender ? output_sender : ws_server_send_coordinator_output;
    s_done_sender = done_sender ? done_sender : ws_server_send_coordinator_done;
    s_session_sender = ws_server_send_subagent_session;
    s_subagent_sender = subagent_sender ? subagent_sender : ws_server_send_subagent_event;
    mutex_unlock(&s_wake_mutex);
}

void delegate_parent_wake_set_activity_window_for_test(long activity_window_ms)
{
    ensure_wake_init();
    mutex_lock(&s_wake_mutex);
    s_parent_activity_window_ms = activity_window_ms;
    mutex_unlock(&s_wake_mutex);
}

int delegate_parent_wake_pending_count_for_test(void)
{
    int count = 0;
    ensure_wake_init();
    mutex_lock(&s_wake_mutex);
    for (int i = 0; i < DELEGATE_PARENT_WAKE_PENDING_MAX; i++) {
        if (s_pending[i].used) {
            count++;
        }
    }
    mutex_unlock(&s_wake_mutex);
    return count;
}
