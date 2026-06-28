#include "delegate_state_json.h"

#include <string.h>

#include "cjson.h"
#include "drivers/memory/session_store.h"
#include "delegate_session_json.h"
#include "drivers/tool/tool_delegate_result_json.h"
#include "kernel/turn/turn_context.h"
#include "linux/slab.h"
#include "text.h"

const char *delegate_wake_state_name(delegate_wake_state_t state)
{
    if (state == DELEGATE_WAKE_PENDING) return "pending";
    if (state == DELEGATE_WAKE_DISPATCHED) return "dispatched";
    if (state == DELEGATE_WAKE_COMPLETED) return "completed";
    return "idle";
}

static void append_interactive_blocker_json(cJSON *array,
                                            const char *chat_id,
                                            const char *task_id,
                                            const char *session_id,
                                            const char *coordinator_id,
                                            const char *request_type,
                                            const char *request_id,
                                            const char *prompt,
                                            const char *blocker_kind,
                                            const char *label)
{
    cJSON *item = NULL;

    if (!array || !request_type || !request_type[0] || !request_id || !request_id[0] ||
        !prompt || !prompt[0]) {
        return;
    }

    item = cJSON_CreateObject();
    if (!item) {
        return;
    }

    cJSON_AddStringToObject(item, "chat_id", chat_id ? chat_id : "");
    cJSON_AddStringToObject(item, "task_id", task_id ? task_id : "");
    cJSON_AddStringToObject(item, "session_id", session_id ? session_id : "");
    cJSON_AddStringToObject(item, "coordinator_id", coordinator_id ? coordinator_id : "");
    cJSON_AddStringToObject(item, "request_type", request_type);
    cJSON_AddStringToObject(item, "request_id", request_id);
    cJSON_AddStringToObject(item, "prompt", prompt);
    if (blocker_kind && blocker_kind[0]) {
        cJSON_AddStringToObject(item, "blocker_kind", blocker_kind);
    }
    if (label && label[0]) {
        cJSON_AddStringToObject(item, "label", label);
    }
    cJSON_AddItemToArray(array, item);
}

static void append_agent_summary_json(cJSON *item,
                                      const delegate_coordinator_agent_view_t *agent,
                                      bool include_full_output)
{
    char preview[256];
    char preferred[512];
    delegate_task_record_t task_snapshot;

    if (!item || !agent) {
        return;
    }
    memset(&task_snapshot, 0, sizeof(task_snapshot));

    cJSON_AddStringToObject(item, "task_id", agent->task_id);
    if (agent->session_id[0]) {
        cJSON_AddStringToObject(item, "session_id", agent->session_id);
    }
    cJSON_AddStringToObject(item, "subagent_type", agent->subagent_type);
    cJSON_AddStringToObject(item, "description", agent->description);
    cJSON_AddStringToObject(item, "status", agent->status);
    if (agent->model[0]) {
        cJSON_AddStringToObject(item, "model", agent->model);
    }
    if (agent->scope_path[0]) {
        cJSON_AddStringToObject(item, "scope_path", agent->scope_path);
    }
    if (agent->scope_kind[0]) {
        cJSON_AddStringToObject(item, "scope_kind", agent->scope_kind);
    }
    if (agent->analysis_focus[0]) {
        cJSON_AddStringToObject(item, "analysis_focus", agent->analysis_focus);
    }
    cJSON_AddNumberToObject(item, "elapsed_ms", agent->elapsed_ms);
    if (delegate_task_store_snapshot(agent->task_id, &task_snapshot) == 0) {
        if (delegate_child_session_preferred_visible_text(&task_snapshot,
                                                          preferred,
                                                          sizeof(preferred))) {
            text_shorten(preferred, preview, sizeof(preview), 180);
            text_sanitize_utf8_json(preview);
            if (preview[0]) {
                cJSON_AddStringToObject(item, "summary", preview);
            }
            if (preferred[0]) {
                cJSON_AddStringToObject(item, "output", preferred);
            }
        }

        if (task_snapshot.output[0]) {
            cJSON_AddStringToObject(item, "raw_output", task_snapshot.output);
        }
        if (include_full_output && task_snapshot.output[0] && !cJSON_GetObjectItemCaseSensitive(item, "output")) {
            cJSON_AddStringToObject(item, "output", task_snapshot.output);
        }
    }
    if (agent->target_files[0]) {
        cJSON_AddStringToObject(item, "target_files", agent->target_files);
    }
    cJSON_AddBoolToObject(item, "write_approved", agent->write_approved);
    if (agent->blocker_kind[0]) {
        cJSON_AddStringToObject(item, "blocker_kind", agent->blocker_kind);
    }
    if (agent->blocker_text[0]) {
        cJSON_AddStringToObject(item, "blocker_text", agent->blocker_text);
    }
}

static void append_child_session_snapshot_json(cJSON *item,
                                               const delegate_coordinator_agent_view_t *agent)
{
    delegate_task_record_t task_snapshot;
    cJSON *child = NULL;
    delegate_child_session_json_options_t options = {
        .history_limit = DELEGATE_CHILD_SESSION_HISTORY_LIMIT_DEFAULT,
    };

    if (!item || !agent) {
        return;
    }
    memset(&task_snapshot, 0, sizeof(task_snapshot));
    if (delegate_task_store_snapshot(agent->task_id, &task_snapshot) != 0) {
        return;
    }
    child = delegate_child_session_json_build_from_task(&task_snapshot, &options);
    if (!child) {
        return;
    }
    cJSON_AddItemToObject(item, "child_session", child);
}

static void append_parent_pending_request_json(cJSON *root,
                                               cJSON *interactive_blockers,
                                               const char *chat_id,
                                               const struct turn_snapshot *turn_snapshot)
{
    cJSON *pending = NULL;
    bool is_question = false;

    if (!root || !interactive_blockers || !chat_id || !turn_snapshot ||
        !turn_snapshot->pending_request_type[0] || !turn_snapshot->pending_request_id[0]) {
        return;
    }

    pending = cJSON_CreateObject();
    if (pending) {
        cJSON_AddStringToObject(pending, "request_type", turn_snapshot->pending_request_type);
        cJSON_AddStringToObject(pending, "request_id", turn_snapshot->pending_request_id);
        cJSON_AddStringToObject(pending, "prompt", turn_snapshot->pending_request_prompt);
        cJSON_AddItemToObject(root, "pending_request", pending);
    }

    is_question = strcmp(turn_snapshot->pending_request_type, "question_text") == 0 ||
                  strcmp(turn_snapshot->pending_request_type, "question") == 0;
    append_interactive_blocker_json(interactive_blockers,
                                    chat_id,
                                    "",
                                    "",
                                    "",
                                    turn_snapshot->pending_request_type,
                                    turn_snapshot->pending_request_id,
                                    turn_snapshot->pending_request_prompt,
                                    is_question ? "question" : "permission",
                                    is_question ? "需要补充信息" : "需要授权");
}

static void append_task_pending_request_json(cJSON *item,
                                             cJSON *interactive_blockers,
                                             const delegate_coordinator_record_t *record,
                                             const delegate_coordinator_agent_view_t *agent)
{
    delegate_task_record_t task_snapshot;
    cJSON *pending = NULL;
    bool is_question = false;

    if (!item || !interactive_blockers || !record || !agent) {
        return;
    }

    memset(&task_snapshot, 0, sizeof(task_snapshot));
    if (delegate_task_store_snapshot(agent->task_id, &task_snapshot) != 0) {
        return;
    }

    if (task_snapshot.output[0]) {
        cJSON_AddStringToObject(item, "raw_output", task_snapshot.output);
    }
    if (!task_snapshot.pending_request.request_type[0]) {
        return;
    }

    pending = cJSON_CreateObject();
    if (pending) {
        cJSON_AddStringToObject(pending, "request_type", task_snapshot.pending_request.request_type);
        cJSON_AddStringToObject(pending, "request_id", task_snapshot.pending_request.request_id);
        cJSON_AddStringToObject(pending, "prompt", task_snapshot.pending_request.prompt_text);
        cJSON_AddItemToObject(item, "pending_request", pending);
    }

    is_question = strcmp(task_snapshot.pending_request.request_type, "question_text") == 0 ||
                  strcmp(task_snapshot.pending_request.request_type, "question") == 0;
    append_interactive_blocker_json(interactive_blockers,
                                    record->chat_id,
                                    task_snapshot.task_id,
                                    task_snapshot.session_id,
                                    task_snapshot.coordinator_id,
                                    task_snapshot.pending_request.request_type,
                                    task_snapshot.pending_request.request_id,
                                    task_snapshot.pending_request.prompt_text,
                                    is_question ? "question" : "permission",
                                    agent->description);
}

char *delegate_coordinator_snapshot_json_build(const delegate_coordinator_record_t *record,
                                               bool include_full_output)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *agents = cJSON_CreateArray();
    cJSON *replay_cursor = cJSON_CreateObject();

    if (!record || !root || !agents || !replay_cursor) {
        cJSON_Delete(root);
        cJSON_Delete(agents);
        cJSON_Delete(replay_cursor);
        return NULL;
    }

    cJSON_AddStringToObject(root, "coordinator_id", record->coordinator_id);
    cJSON_AddStringToObject(root, "chat_id", record->chat_id);
    if (record->team_run_id[0]) {
        cJSON_AddStringToObject(root, "team_run_id", record->team_run_id);
    }
    if (record->team_name[0]) {
        cJSON_AddStringToObject(root, "team_name", record->team_name);
    }
    if (record->dispatch_mode[0]) {
        cJSON_AddStringToObject(root, "dispatch_mode", record->dispatch_mode);
    }
    cJSON_AddStringToObject(root, "status", record->status);
    cJSON_AddNumberToObject(root, "agent_count", record->agent_count);
    cJSON_AddNumberToObject(root, "completed_count", record->completed_count);
    cJSON_AddNumberToObject(root, "running_count", record->running_count);
    cJSON_AddNumberToObject(root, "queued_count", record->queued_count);
    cJSON_AddNumberToObject(root, "blocked_count", record->blocked_count);
    cJSON_AddNumberToObject(root, "failed_count", record->failed_count);
    cJSON_AddNumberToObject(root, "effective_output_count", record->effective_output_count);
    cJSON_AddNumberToObject(root, "visible_revision", (double)record->visible_revision);
    cJSON_AddNumberToObject(replay_cursor, "visible_revision", (double)record->visible_revision);
    cJSON_AddItemToObject(root, "replay_cursor", replay_cursor);
    cJSON_AddBoolToObject(root, "completion_notified", record->completion_notified);
    cJSON_AddBoolToObject(root, "parent_response_sent", record->parent_response_sent);
    cJSON_AddBoolToObject(root, "parent_resume_enqueued", record->parent_resume_enqueued);
    cJSON_AddStringToObject(root, "wake_state", delegate_wake_state_name(record->wake_state));
    cJSON_AddNumberToObject(root, "wake_retry_count", record->wake_retry_count);
    cJSON_AddNumberToObject(root, "wake_last_attempt_ms", record->wake_last_attempt_ms);
    cJSON_AddNumberToObject(root, "wake_last_success_ms", record->wake_last_success_ms);
    if (record->wake_last_error[0]) {
        cJSON_AddStringToObject(root, "wake_last_error", record->wake_last_error);
    }

    for (int i = 0; i < record->agent_count; i++) {
        const delegate_coordinator_agent_view_t *agent = &record->agents[i];
        cJSON *item = cJSON_CreateObject();

        if (!item) {
            continue;
        }
        cJSON_AddStringToObject(item, "name", agent->description[0] ? agent->description : agent->subagent_type);
        append_agent_summary_json(item, agent, include_full_output);
        if (agent->task_key[0]) {
            cJSON_AddStringToObject(item, "task_key", agent->task_key);
        }
        if (agent->depends_on[0]) {
            cJSON_AddStringToObject(item, "depends_on", agent->depends_on);
        }
        append_child_session_snapshot_json(item, agent);
        cJSON_AddBoolToObject(item, "parent_response_sent", record->parent_response_sent);
        cJSON_AddBoolToObject(item, "parent_resume_enqueued", record->parent_resume_enqueued);
        cJSON_AddStringToObject(item, "coordinator_status", record->status);
        cJSON_AddStringToObject(item, "coordinator_wake_state", delegate_wake_state_name(record->wake_state));
        cJSON_AddNumberToObject(item, "wake_retry_count", record->wake_retry_count);
        if (record->wake_last_error[0]) {
            cJSON_AddStringToObject(item, "wake_last_error", record->wake_last_error);
        }
        if (record->blocker_kind[0]) {
            cJSON_AddStringToObject(item, "coordinator_blocker_kind", record->blocker_kind);
        }
        if (record->blocker_text[0]) {
            cJSON_AddStringToObject(item, "coordinator_blocker_text", record->blocker_text);
        }
        cJSON_AddItemToArray(agents, item);
    }

    if (record->blocker_kind[0]) {
        cJSON_AddStringToObject(root, "blocker_kind", record->blocker_kind);
    }
    if (record->blocker_text[0]) {
        cJSON_AddStringToObject(root, "blocker_text", record->blocker_text);
    }
    cJSON_AddItemToObject(root, "agents", agents);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

char *delegate_coordinator_completion_json_build(const delegate_coordinator_record_t *record)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *items = cJSON_CreateArray();

    if (!record || !root || !items) {
        cJSON_Delete(root);
        cJSON_Delete(items);
        return NULL;
    }

    cJSON_AddStringToObject(root, "coordinator_id", record->coordinator_id);
    cJSON_AddStringToObject(root, "status", record->status);
    cJSON_AddNumberToObject(root, "effective_output_count", record->effective_output_count);
    for (int i = 0; i < record->agent_count; i++) {
        const delegate_coordinator_agent_view_t *agent = &record->agents[i];
        cJSON *item = cJSON_CreateObject();

        if (!item) {
            continue;
        }
        append_agent_summary_json(item, agent, false);
        append_child_session_snapshot_json(item, agent);
        cJSON_AddItemToArray(items, item);
    }
    cJSON_AddItemToObject(root, "agents", items);

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return payload;
}

char *delegate_subagent_session_payload_json_build(const delegate_coordinator_record_t *record,
                                                   const delegate_coordinator_agent_view_t *agent)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *item = cJSON_CreateObject();
    cJSON *interactive = cJSON_CreateObject();
    cJSON *blockers = cJSON_CreateArray();
    cJSON *replay_cursor = cJSON_CreateObject();

    if (!root || !item || !interactive || !blockers || !replay_cursor) {
        cJSON_Delete(root);
        cJSON_Delete(item);
        cJSON_Delete(interactive);
        cJSON_Delete(blockers);
        cJSON_Delete(replay_cursor);
        return NULL;
    }

    cJSON_AddStringToObject(root, "coordinator_id", record ? record->coordinator_id : "");
    cJSON_AddStringToObject(root, "chat_id", record ? record->chat_id : "");
    cJSON_AddNumberToObject(root, "visible_revision", record ? (double)record->visible_revision : 0.0);
    cJSON_AddNumberToObject(replay_cursor, "visible_revision", record ? (double)record->visible_revision : 0.0);
    cJSON_AddItemToObject(root, "replay_cursor", replay_cursor);
    cJSON_AddStringToObject(root, "task_id", agent ? agent->task_id : "");
    cJSON_AddStringToObject(root, "session_id", agent ? agent->session_id : "");
    cJSON_AddStringToObject(root, "subagent_type", agent ? agent->subagent_type : "");
    cJSON_AddStringToObject(root, "status", agent ? agent->status : "");
    cJSON_AddStringToObject(root, "task", agent ? agent->description : "");
    if (agent && agent->task_key[0]) {
        cJSON_AddStringToObject(root, "task_key", agent->task_key);
    }
    if (agent && agent->depends_on[0]) {
        cJSON_AddStringToObject(root, "depends_on", agent->depends_on);
    }
    cJSON_AddItemToObject(interactive, "blockers", blockers);
    cJSON_AddItemToObject(root, "interactive", interactive);
    append_agent_summary_json(item, agent, true);
    append_child_session_snapshot_json(item, agent);
    cJSON_AddItemToObject(root, "agent", item);

    if (agent && agent->task_id[0]) {
        delegate_task_record_t task_snapshot;
        bool is_question = false;

        memset(&task_snapshot, 0, sizeof(task_snapshot));
        if (delegate_task_store_snapshot(agent->task_id, &task_snapshot) == 0 &&
            task_snapshot.pending_request.request_type[0] &&
            task_snapshot.pending_request.request_id[0] &&
            task_snapshot.pending_request.prompt_text[0]) {
            is_question = strcmp(task_snapshot.pending_request.request_type, "question_text") == 0 ||
                          strcmp(task_snapshot.pending_request.request_type, "question") == 0;
            append_interactive_blocker_json(blockers,
                                            record ? record->chat_id : "",
                                            task_snapshot.task_id,
                                            task_snapshot.session_id,
                                            task_snapshot.coordinator_id,
                                            task_snapshot.pending_request.request_type,
                                            task_snapshot.pending_request.request_id,
                                            task_snapshot.pending_request.prompt_text,
                                            is_question ? "question" : "permission",
                                            agent->description);
        }
    }

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return payload;
}

char *delegate_subagent_session_delta_json_build(const char *task_id,
                                                 unsigned long history_after_seq,
                                                 unsigned long frame_after_seq,
                                                 unsigned long commit_after_seq)
{
    delegate_task_record_t task_snapshot;
    cJSON *root = NULL;
    cJSON *child = NULL;
    delegate_child_session_json_options_t options = {
        .history_limit = DELEGATE_CHILD_SESSION_HISTORY_LIMIT_DEFAULT,
        .history_after_seq = history_after_seq,
        .frame_after_seq = frame_after_seq,
        .commit_after_seq = commit_after_seq,
    };

    if (!task_id || !task_id[0]) {
        return NULL;
    }

    memset(&task_snapshot, 0, sizeof(task_snapshot));
    if (delegate_task_store_snapshot(task_id, &task_snapshot) != 0) {
        return NULL;
    }

    child = delegate_child_session_json_build_from_task(&task_snapshot, &options);
    if (!child) {
        return NULL;
    }

    root = cJSON_CreateObject();
    if (!root) {
        cJSON_Delete(child);
        return NULL;
    }

    cJSON_AddStringToObject(root, "task_id", task_snapshot.task_id);
    cJSON_AddStringToObject(root, "session_id", task_snapshot.session_id);
    cJSON_AddStringToObject(root, "coordinator_id", task_snapshot.coordinator_id);
    cJSON_AddStringToObject(root, "subagent_type", task_snapshot.subagent_type);
    cJSON_AddStringToObject(root,
                            "status",
                            task_snapshot.status == DELEGATE_TASK_DONE
                                ? "done"
                                : task_snapshot.status == DELEGATE_TASK_FAILED
                                    ? "failed"
                                    : task_snapshot.status == DELEGATE_TASK_QUEUED
                                        ? "queued"
                                        : "running");
    cJSON_AddNumberToObject(root, "history_after_seq", (double)history_after_seq);
    cJSON_AddNumberToObject(root, "frame_after_seq", (double)frame_after_seq);
    cJSON_AddNumberToObject(root, "commit_after_seq", (double)commit_after_seq);
    cJSON_AddItemToObject(root, "child_session", child);

    {
        char *payload = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        return payload;
    }
}

char *delegate_subagent_session_deltas_json_build(const char *chat_id,
                                                  const char *request_json)
{
    delegate_parent_registry_view_t parent_view;
    cJSON *request = NULL;
    cJSON *tasks = NULL;
    cJSON *root = NULL;
    cJSON *items = NULL;
    int appended = 0;

    if (!chat_id || !chat_id[0] || !request_json || !request_json[0]) {
        return NULL;
    }

    memset(&parent_view, 0, sizeof(parent_view));
    if (delegate_task_store_snapshot_parent(chat_id, &parent_view) != 0) {
        return NULL;
    }

    request = cJSON_Parse(request_json);
    if (!request) {
        return NULL;
    }

    tasks = cJSON_GetObjectItemCaseSensitive(request, "tasks");
    if (!tasks || !cJSON_IsArray(tasks)) {
        cJSON_Delete(request);
        return NULL;
    }

    root = cJSON_CreateObject();
    items = cJSON_CreateArray();
    if (!root || !items) {
        cJSON_Delete(root);
        cJSON_Delete(items);
        cJSON_Delete(request);
        return NULL;
    }

    cJSON_AddStringToObject(root, "chat_id", chat_id);
    cJSON_AddItemToObject(root, "items", items);

    cJSON *task = NULL;
    cJSON_ArrayForEach(task, tasks) {
        const cJSON *task_id_item = NULL;
        const char *task_id = NULL;
        unsigned long history_after_seq = 0;
        unsigned long frame_after_seq = 0;
        unsigned long commit_after_seq = 0;
        bool matches_parent = false;
        char *payload = NULL;
        cJSON *item = NULL;

        if (!cJSON_IsObject(task)) {
            continue;
        }

        task_id_item = cJSON_GetObjectItemCaseSensitive(task, "task_id");
        if (!cJSON_IsString(task_id_item) || !task_id_item->valuestring || !task_id_item->valuestring[0]) {
            continue;
        }
        task_id = task_id_item->valuestring;

        for (int i = 0; i < parent_view.task_count; i++) {
            if (strcmp(parent_view.tasks[i].task_id, task_id) == 0) {
                matches_parent = true;
                break;
            }
        }
        if (!matches_parent) {
            continue;
        }

        {
            cJSON *item_json = cJSON_GetObjectItemCaseSensitive(task, "history_after_seq");
            if (cJSON_IsNumber(item_json) && item_json->valuedouble > 0) {
                history_after_seq = (unsigned long)item_json->valuedouble;
            }
            item_json = cJSON_GetObjectItemCaseSensitive(task, "frame_after_seq");
            if (cJSON_IsNumber(item_json) && item_json->valuedouble > 0) {
                frame_after_seq = (unsigned long)item_json->valuedouble;
            }
            item_json = cJSON_GetObjectItemCaseSensitive(task, "commit_after_seq");
            if (cJSON_IsNumber(item_json) && item_json->valuedouble > 0) {
                commit_after_seq = (unsigned long)item_json->valuedouble;
            }
        }

        payload = delegate_subagent_session_delta_json_build(task_id,
                                                             history_after_seq,
                                                             frame_after_seq,
                                                             commit_after_seq);
        if (!payload) {
            continue;
        }

        item = cJSON_Parse(payload);
        kfree(payload);
        if (!item) {
            continue;
        }

        cJSON_AddItemToArray(items, item);
        appended++;
    }

    cJSON_AddNumberToObject(root, "item_count", appended);

    {
        char *json = appended > 0 ? cJSON_PrintUnformatted(root) : NULL;
        cJSON_Delete(root);
        cJSON_Delete(request);
        return json;
    }
}

char *delegate_parent_subagent_state_delta_json_build(const char *chat_id,
                                                      unsigned long after_visible_revision,
                                                      const char *request_json)
{
    delegate_parent_registry_view_t parent_view;
    cJSON *request = NULL;
    cJSON *tasks = NULL;
    cJSON *root = NULL;
    cJSON *coordinators = NULL;
    cJSON *items = NULL;
    cJSON *replay_cursor = NULL;
    unsigned long max_visible_revision = after_visible_revision;
    int changed_count = 0;
    int item_count = 0;
    char changed_coordinator_ids[DELEGATE_COORDINATOR_STORE_MAX][DELEGATE_COORDINATOR_ID_LEN];
    int changed_coordinator_count = 0;
    char requested_task_ids[DELEGATE_TASK_STORE_MAX][DELEGATE_TASK_ID_LEN];
    int requested_task_count = 0;

    if (!chat_id || !chat_id[0]) {
        return NULL;
    }

    memset(&parent_view, 0, sizeof(parent_view));
    if (delegate_task_store_snapshot_parent(chat_id, &parent_view) != 0) {
        return NULL;
    }

    if (request_json && request_json[0]) {
        request = cJSON_Parse(request_json);
        if (!request || !cJSON_IsObject(request)) {
            cJSON_Delete(request);
            return NULL;
        }
        tasks = cJSON_GetObjectItemCaseSensitive(request, "tasks");
    }

    root = cJSON_CreateObject();
    coordinators = cJSON_CreateArray();
    items = cJSON_CreateArray();
    replay_cursor = cJSON_CreateObject();
    if (!root || !coordinators || !items || !replay_cursor) {
        cJSON_Delete(root);
        cJSON_Delete(coordinators);
        cJSON_Delete(items);
        cJSON_Delete(replay_cursor);
        cJSON_Delete(request);
        return NULL;
    }

    cJSON_AddStringToObject(root, "chat_id", chat_id);

    for (int i = 0; i < parent_view.coordinator_count; i++) {
        const delegate_parent_coordinator_list_item_t *summary = &parent_view.coordinators[i];
        delegate_coordinator_record_t snapshot;

        memset(&snapshot, 0, sizeof(snapshot));
        if (delegate_task_store_snapshot_coordinator(summary->coordinator_id, &snapshot) != 0) {
            continue;
        }
        if (snapshot.visible_revision <= after_visible_revision) {
            if (snapshot.visible_revision > max_visible_revision) {
                max_visible_revision = snapshot.visible_revision;
            }
            continue;
        }

        char *coord_json = delegate_coordinator_snapshot_json_build(&snapshot, false);
        if (coord_json) {
            cJSON *coord = cJSON_Parse(coord_json);
            kfree(coord_json);
            if (coord) {
                cJSON_AddItemToArray(coordinators, coord);
                changed_count++;
                if (changed_coordinator_count < DELEGATE_COORDINATOR_STORE_MAX) {
                    snprintf(changed_coordinator_ids[changed_coordinator_count],
                             sizeof(changed_coordinator_ids[0]),
                             "%s",
                             snapshot.coordinator_id);
                    changed_coordinator_count++;
                }
            }
        }
        if (snapshot.visible_revision > max_visible_revision) {
            max_visible_revision = snapshot.visible_revision;
        }
    }

    if (cJSON_IsArray(tasks)) {
        cJSON *task = NULL;
        cJSON_ArrayForEach(task, tasks) {
            cJSON *task_id_item = NULL;
            const char *task_id = NULL;
            unsigned long history_after_seq = 0;
            unsigned long frame_after_seq = 0;
            unsigned long commit_after_seq = 0;
            bool allowed = false;
            char *payload = NULL;
            cJSON *item = NULL;

            if (!cJSON_IsObject(task)) {
                continue;
            }

            task_id_item = cJSON_GetObjectItemCaseSensitive(task, "task_id");
            if (!cJSON_IsString(task_id_item) || !task_id_item->valuestring || !task_id_item->valuestring[0]) {
                continue;
            }
            task_id = task_id_item->valuestring;
            if (requested_task_count < DELEGATE_TASK_STORE_MAX) {
                snprintf(requested_task_ids[requested_task_count],
                         sizeof(requested_task_ids[0]),
                         "%s",
                         task_id);
                requested_task_count++;
            }

            for (int i = 0; i < parent_view.task_count; i++) {
                if (strcmp(parent_view.tasks[i].task_id, task_id) == 0) {
                    allowed = true;
                    break;
                }
            }
            if (!allowed) {
                continue;
            }

            {
                cJSON *item_json = cJSON_GetObjectItemCaseSensitive(task, "history_after_seq");
                if (cJSON_IsNumber(item_json) && item_json->valuedouble > 0) {
                    history_after_seq = (unsigned long)item_json->valuedouble;
                }
                item_json = cJSON_GetObjectItemCaseSensitive(task, "frame_after_seq");
                if (cJSON_IsNumber(item_json) && item_json->valuedouble > 0) {
                    frame_after_seq = (unsigned long)item_json->valuedouble;
                }
                item_json = cJSON_GetObjectItemCaseSensitive(task, "commit_after_seq");
                if (cJSON_IsNumber(item_json) && item_json->valuedouble > 0) {
                    commit_after_seq = (unsigned long)item_json->valuedouble;
                }
            }

            payload = delegate_subagent_session_delta_json_build(task_id,
                                                                 history_after_seq,
                                                                 frame_after_seq,
                                                                 commit_after_seq);
            if (!payload) {
                continue;
            }

            item = cJSON_Parse(payload);
            kfree(payload);
            if (!item) {
                continue;
            }

            cJSON_AddItemToArray(items, item);
            item_count++;
        }
    }

    for (int i = 0; i < parent_view.task_count; i++) {
        const delegate_parent_task_list_item_t *task_item = &parent_view.tasks[i];
        bool coordinator_changed = false;
        bool already_requested = false;
        char *payload = NULL;
        cJSON *item = NULL;

        if (!task_item->task_id[0] || !task_item->coordinator_id[0]) {
            continue;
        }

        for (int coord_idx = 0; coord_idx < changed_coordinator_count; coord_idx++) {
            if (strcmp(changed_coordinator_ids[coord_idx], task_item->coordinator_id) == 0) {
                coordinator_changed = true;
                break;
            }
        }
        if (!coordinator_changed) {
            continue;
        }

        for (int task_idx = 0; task_idx < requested_task_count; task_idx++) {
            if (strcmp(requested_task_ids[task_idx], task_item->task_id) == 0) {
                already_requested = true;
                break;
            }
        }
        if (already_requested) {
            continue;
        }

        payload = delegate_subagent_session_delta_json_build(task_item->task_id, 0, 0, 0);
        if (!payload) {
            continue;
        }

        item = cJSON_Parse(payload);
        kfree(payload);
        if (!item) {
            continue;
        }

        cJSON_AddItemToArray(items, item);
        item_count++;
    }

    cJSON_AddNumberToObject(root, "after_visible_revision", (double)after_visible_revision);
    cJSON_AddNumberToObject(root, "max_visible_revision", (double)max_visible_revision);
    cJSON_AddNumberToObject(replay_cursor, "after_visible_revision", (double)after_visible_revision);
    cJSON_AddNumberToObject(replay_cursor, "visible_revision", (double)max_visible_revision);
    cJSON_AddItemToObject(root, "replay_cursor", replay_cursor);
    cJSON_AddNumberToObject(root, "changed_count", changed_count);
    cJSON_AddNumberToObject(root, "item_count", item_count);
    cJSON_AddItemToObject(root, "coordinators", coordinators);
    cJSON_AddItemToObject(root, "items", items);

    {
        char *json = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        cJSON_Delete(request);
        return json;
    }
}

char *delegate_parent_subagent_state_json_build(const char *chat_id)
{
    delegate_parent_registry_view_t parent_view;
    struct turn_snapshot turn_snapshot;
    cJSON *root = NULL;
    cJSON *coordinators = NULL;
    cJSON *interactive = NULL;
    cJSON *interactive_blockers = NULL;
    cJSON *replay_cursor = NULL;
    bool has_turn_snapshot = false;
    unsigned long max_visible_revision = 0;

    if (!chat_id || !chat_id[0]) {
        return NULL;
    }

    memset(&parent_view, 0, sizeof(parent_view));
    memset(&turn_snapshot, 0, sizeof(turn_snapshot));
    has_turn_snapshot = turn_context_load_copy(chat_id, &turn_snapshot);
    if (delegate_task_store_snapshot_parent(chat_id, &parent_view) != 0) {
        memset(&parent_view, 0, sizeof(parent_view));
        if (!has_turn_snapshot || !turn_snapshot.pending_request_type[0] || !turn_snapshot.pending_request_id[0]) {
            turn_context_snapshot_cleanup(&turn_snapshot);
            return NULL;
        }
    }

    root = cJSON_CreateObject();
    coordinators = cJSON_CreateArray();
    interactive = cJSON_CreateObject();
    interactive_blockers = cJSON_CreateArray();
    replay_cursor = cJSON_CreateObject();
    if (!root || !coordinators || !interactive || !interactive_blockers || !replay_cursor) {
        cJSON_Delete(root);
        cJSON_Delete(coordinators);
        cJSON_Delete(interactive);
        cJSON_Delete(interactive_blockers);
        cJSON_Delete(replay_cursor);
        turn_context_snapshot_cleanup(&turn_snapshot);
        return NULL;
    }

    cJSON_AddStringToObject(root, "chat_id", chat_id);
    cJSON_AddNumberToObject(root, "coordinator_count", parent_view.coordinator_count);
    cJSON_AddItemToObject(root, "coordinators", coordinators);
    cJSON_AddItemToObject(interactive, "blockers", interactive_blockers);
    cJSON_AddItemToObject(root, "interactive", interactive);
    if (has_turn_snapshot) {
        append_parent_pending_request_json(root, interactive_blockers, chat_id, &turn_snapshot);
        turn_context_snapshot_cleanup(&turn_snapshot);
    }

    for (int i = 0; i < parent_view.coordinator_count; i++) {
        delegate_coordinator_record_t snapshot;
        const delegate_parent_coordinator_list_item_t *summary = &parent_view.coordinators[i];
        cJSON *coord = NULL;
        cJSON *agents = NULL;

        memset(&snapshot, 0, sizeof(snapshot));
        if (delegate_task_store_snapshot_coordinator(summary->coordinator_id, &snapshot) != 0) {
            continue;
        }
        if (snapshot.visible_revision > max_visible_revision) {
            max_visible_revision = snapshot.visible_revision;
        }

        coord = cJSON_CreateObject();
        agents = cJSON_CreateArray();
        if (!coord || !agents) {
            cJSON_Delete(coord);
            cJSON_Delete(agents);
            continue;
        }

        cJSON_AddStringToObject(coord, "coordinator_id", snapshot.coordinator_id);
        cJSON_AddStringToObject(coord, "chat_id", snapshot.chat_id);
        if (snapshot.team_run_id[0]) {
            cJSON_AddStringToObject(coord, "team_run_id", snapshot.team_run_id);
        }
        if (snapshot.team_name[0]) {
            cJSON_AddStringToObject(coord, "team_name", snapshot.team_name);
        }
        if (snapshot.dispatch_mode[0]) {
            cJSON_AddStringToObject(coord, "dispatch_mode", snapshot.dispatch_mode);
        }
        cJSON_AddStringToObject(coord, "status", snapshot.status);
        cJSON_AddNumberToObject(coord, "agent_count", snapshot.agent_count);
        cJSON_AddNumberToObject(coord, "completed_count", snapshot.completed_count);
        cJSON_AddNumberToObject(coord, "running_count", snapshot.running_count);
        cJSON_AddNumberToObject(coord, "queued_count", snapshot.queued_count);
        cJSON_AddNumberToObject(coord, "blocked_count", snapshot.blocked_count);
        cJSON_AddNumberToObject(coord, "failed_count", snapshot.failed_count);
        cJSON_AddNumberToObject(coord, "effective_output_count", snapshot.effective_output_count);
        cJSON_AddNumberToObject(coord, "visible_revision", (double)snapshot.visible_revision);
        cJSON_AddBoolToObject(coord, "completion_notified", snapshot.completion_notified);
        cJSON_AddBoolToObject(coord, "parent_response_sent", snapshot.parent_response_sent);
        cJSON_AddBoolToObject(coord, "parent_resume_enqueued", snapshot.parent_resume_enqueued);
        cJSON_AddStringToObject(coord, "wake_state", delegate_wake_state_name(snapshot.wake_state));
        cJSON_AddNumberToObject(coord, "wake_retry_count", snapshot.wake_retry_count);
        cJSON_AddNumberToObject(coord, "wake_last_attempt_ms", snapshot.wake_last_attempt_ms);
        cJSON_AddNumberToObject(coord, "wake_last_success_ms", snapshot.wake_last_success_ms);
        if (snapshot.wake_last_error[0]) {
            cJSON_AddStringToObject(coord, "wake_last_error", snapshot.wake_last_error);
        }
        if (snapshot.blocker_kind[0]) {
            cJSON_AddStringToObject(coord, "blocker_kind", snapshot.blocker_kind);
        }
        if (snapshot.blocker_text[0]) {
            cJSON_AddStringToObject(coord, "blocker_text", snapshot.blocker_text);
        }

        for (int j = 0; j < snapshot.agent_count; j++) {
            const delegate_coordinator_agent_view_t *agent = &snapshot.agents[j];
            cJSON *item = cJSON_CreateObject();

            if (!item) {
                continue;
            }

            cJSON_AddStringToObject(item, "name", agent->description[0] ? agent->description : agent->subagent_type);
            append_agent_summary_json(item, agent, false);
            if (agent->task_key[0]) {
                cJSON_AddStringToObject(item, "task_key", agent->task_key);
            }
            if (agent->depends_on[0]) {
                cJSON_AddStringToObject(item, "depends_on", agent->depends_on);
            }
            append_child_session_snapshot_json(item, agent);
            cJSON_AddBoolToObject(item, "parent_response_sent", snapshot.parent_response_sent);
            cJSON_AddBoolToObject(item, "parent_resume_enqueued", snapshot.parent_resume_enqueued);
            cJSON_AddStringToObject(item, "coordinator_status", snapshot.status);
            cJSON_AddStringToObject(item, "coordinator_wake_state", delegate_wake_state_name(snapshot.wake_state));
            cJSON_AddNumberToObject(item, "wake_retry_count", snapshot.wake_retry_count);
            if (snapshot.wake_last_error[0]) {
                cJSON_AddStringToObject(item, "wake_last_error", snapshot.wake_last_error);
            }
            if (snapshot.blocker_kind[0]) {
                cJSON_AddStringToObject(item, "coordinator_blocker_kind", snapshot.blocker_kind);
            }
            if (snapshot.blocker_text[0]) {
                cJSON_AddStringToObject(item, "coordinator_blocker_text", snapshot.blocker_text);
            }
            append_task_pending_request_json(item, interactive_blockers, &snapshot, agent);
            cJSON_AddItemToArray(agents, item);
        }

        cJSON_AddItemToObject(coord, "agents", agents);
        cJSON_AddItemToArray(coordinators, coord);
    }

    cJSON_AddNumberToObject(root, "visible_revision", (double)max_visible_revision);
    cJSON_AddNumberToObject(replay_cursor, "after_visible_revision", 0.0);
    cJSON_AddNumberToObject(replay_cursor, "visible_revision", (double)max_visible_revision);
    cJSON_AddItemToObject(root, "replay_cursor", replay_cursor);
    replay_cursor = NULL;

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    cJSON_Delete(replay_cursor);
    return json;
}
