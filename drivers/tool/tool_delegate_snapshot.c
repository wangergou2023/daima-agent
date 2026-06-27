#include "drivers/tool/tool_delegate_snapshot.h"

#include <string.h>

#include "cjson.h"
#include "delegate/delegate_task_store.h"
#include "drivers/tool/tool_delegate_summary.h"
#include "linux/kernel.h"
#include "linux/slab.h"

static bool delegate_task_store_snapshot_quiet_local(const char *task_id,
                                                     delegate_task_record_t *out)
{
    return delegate_task_store_snapshot(task_id, out) == 0;
}

static err_t continue_background_coordinator(const char *coordinator_id,
                                             char *output,
                                             size_t output_size)
{
    delegate_coordinator_record_t *record = kzalloc(sizeof(*record), GFP_KERNEL);
    char summary[2048];

    if (!record) {
        return ERR_NO_MEM;
    }
    memset(summary, 0, sizeof(summary));

    err_t err = delegate_task_store_snapshot_coordinator(coordinator_id, record);
    if (err != 0) {
        snprintf(output, output_size, "delegate_task: coordinator_id not found: %s", coordinator_id ? coordinator_id : "");
        kfree(record);
        return err;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *agents = cJSON_CreateArray();
    if (!root || !agents) {
        cJSON_Delete(root);
        cJSON_Delete(agents);
        kfree(record);
        return ERR_NO_MEM;
    }
    cJSON_AddStringToObject(root, "coordinator_id", record->coordinator_id);
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
    cJSON_AddNumberToObject(root, "completed_count", record->completed_count);
    cJSON_AddNumberToObject(root, "agent_count", record->agent_count);
    cJSON_AddNumberToObject(root, "running_count", record->running_count);
    cJSON_AddNumberToObject(root, "queued_count", record->queued_count);
    cJSON_AddNumberToObject(root, "blocked_count", record->blocked_count);
    cJSON_AddNumberToObject(root, "failed_count", record->failed_count);

    for (int i = 0; i < record->agent_count; i++) {
        cJSON *item = cJSON_CreateObject();
        delegate_task_record_t task_snapshot;
        memset(&task_snapshot, 0, sizeof(task_snapshot));

        cJSON_AddStringToObject(item, "task_id", record->agents[i].task_id);
        if (record->agents[i].session_id[0]) {
            cJSON_AddStringToObject(item, "session_id", record->agents[i].session_id);
        }
        cJSON_AddStringToObject(item, "subagent_type", record->agents[i].subagent_type);
        cJSON_AddStringToObject(item, "description", record->agents[i].description);
        if (record->agents[i].task_key[0]) {
            cJSON_AddStringToObject(item, "task_key", record->agents[i].task_key);
        }
        if (record->agents[i].depends_on[0]) {
            cJSON_AddStringToObject(item, "depends_on", record->agents[i].depends_on);
        }
        cJSON_AddStringToObject(item, "status", record->agents[i].status);
        if (record->agents[i].model[0]) {
            cJSON_AddStringToObject(item, "model", record->agents[i].model);
        }
        if (record->agents[i].scope_path[0]) {
            cJSON_AddStringToObject(item, "scope_path", record->agents[i].scope_path);
        }
        if (record->agents[i].scope_kind[0]) {
            cJSON_AddStringToObject(item, "scope_kind", record->agents[i].scope_kind);
        }
        if (record->agents[i].analysis_focus[0]) {
            cJSON_AddStringToObject(item, "analysis_focus", record->agents[i].analysis_focus);
        }
        cJSON_AddNumberToObject(item, "elapsed_ms", record->agents[i].elapsed_ms);
        if (delegate_task_store_snapshot_quiet_local(record->agents[i].task_id, &task_snapshot) &&
            task_snapshot.output[0]) {
            cJSON_AddStringToObject(item, "output", task_snapshot.output);
        }
        if (record->agents[i].target_files[0]) {
            cJSON_AddStringToObject(item, "target_files", record->agents[i].target_files);
        }
        cJSON_AddBoolToObject(item, "write_approved", record->agents[i].write_approved);
        cJSON_AddItemToArray(agents, item);
    }

    cJSON_AddItemToObject(root, "agents", agents);
    tool_delegate_render_background_coordinator_summary(record, summary, sizeof(summary));
    if (summary[0]) {
        cJSON_AddStringToObject(root, "summary", summary);
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) {
        kfree(record);
        return ERR_NO_MEM;
    }

    strscpy(output, json, output_size);
    kfree(json);
    kfree(record);
    return 0;
}

err_t tool_delegate_render_background_coordinator_snapshot(const char *coordinator_id,
                                                           char *output,
                                                           size_t output_size)
{
    return continue_background_coordinator(coordinator_id, output, output_size);
}

err_t tool_delegate_render_parent_registry_view(const char *chat_id,
                                                char *output,
                                                size_t output_size)
{
    delegate_parent_registry_view_t view;
    memset(&view, 0, sizeof(view));

    err_t err = delegate_task_store_snapshot_parent(chat_id, &view);
    if (err != 0) {
        snprintf(output, output_size, "delegate_task: no delegated tasks found for chat_id: %s",
                 chat_id ? chat_id : "");
        return err;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *coordinators = cJSON_CreateArray();
    cJSON *tasks = cJSON_CreateArray();
    if (!root || !coordinators || !tasks) {
        cJSON_Delete(root);
        cJSON_Delete(coordinators);
        cJSON_Delete(tasks);
        return ERR_NO_MEM;
    }

    cJSON_AddStringToObject(root, "action", "list");
    cJSON_AddStringToObject(root, "scope", "parent");
    cJSON_AddStringToObject(root, "chat_id", view.chat_id);
    cJSON_AddNumberToObject(root, "coordinator_count", view.coordinator_count);
    cJSON_AddNumberToObject(root, "task_count", view.task_count);

    for (int i = 0; i < view.coordinator_count; i++) {
        const delegate_parent_coordinator_list_item_t *record = &view.coordinators[i];
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "coordinator_id", record->coordinator_id);
        cJSON_AddStringToObject(item, "chat_id", record->chat_id);
        if (record->team_run_id[0]) {
            cJSON_AddStringToObject(item, "team_run_id", record->team_run_id);
        }
        if (record->team_name[0]) {
            cJSON_AddStringToObject(item, "team_name", record->team_name);
        }
        if (record->dispatch_mode[0]) {
            cJSON_AddStringToObject(item, "dispatch_mode", record->dispatch_mode);
        }
        cJSON_AddStringToObject(item, "status", record->status);
        cJSON_AddNumberToObject(item, "agent_count", record->agent_count);
        cJSON_AddNumberToObject(item, "completed_count", record->completed_count);
        cJSON_AddNumberToObject(item, "running_count", record->running_count);
        cJSON_AddNumberToObject(item, "queued_count", record->queued_count);
        cJSON_AddNumberToObject(item, "blocked_count", record->blocked_count);
        cJSON_AddNumberToObject(item, "failed_count", record->failed_count);
        cJSON_AddNumberToObject(item, "effective_output_count", record->effective_output_count);
        cJSON_AddBoolToObject(item, "completion_notified", record->completion_notified);
        cJSON_AddBoolToObject(item, "parent_response_sent", record->parent_response_sent);
        cJSON_AddBoolToObject(item, "parent_resume_enqueued", record->parent_resume_enqueued);
        cJSON_AddStringToObject(item, "wake_state",
                                record->wake_state == DELEGATE_WAKE_PENDING ? "pending" :
                                record->wake_state == DELEGATE_WAKE_DISPATCHED ? "dispatched" :
                                record->wake_state == DELEGATE_WAKE_COMPLETED ? "completed" : "idle");
        cJSON_AddNumberToObject(item, "wake_retry_count", record->wake_retry_count);
        cJSON_AddNumberToObject(item, "wake_last_attempt_ms", record->wake_last_attempt_ms);
        cJSON_AddNumberToObject(item, "wake_last_success_ms", record->wake_last_success_ms);
        if (record->wake_last_error[0]) {
            cJSON_AddStringToObject(item, "wake_last_error", record->wake_last_error);
        }
        cJSON_AddItemToArray(coordinators, item);
    }

    for (int i = 0; i < view.task_count; i++) {
        const delegate_parent_task_list_item_t *record = &view.tasks[i];
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "task_id", record->task_id);
        cJSON_AddStringToObject(item, "coordinator_id", record->coordinator_id);
        if (record->session_id[0]) {
            cJSON_AddStringToObject(item, "session_id", record->session_id);
        }
        cJSON_AddStringToObject(item, "subagent_type", record->subagent_type);
        cJSON_AddStringToObject(item, "description", record->description);
        if (record->task_key[0]) {
            cJSON_AddStringToObject(item, "task_key", record->task_key);
        }
        if (record->depends_on[0]) {
            cJSON_AddStringToObject(item, "depends_on", record->depends_on);
        }
        if (record->model[0]) {
            cJSON_AddStringToObject(item, "model", record->model);
        }
        if (record->scope_path[0]) {
            cJSON_AddStringToObject(item, "scope_path", record->scope_path);
        }
        if (record->scope_kind[0]) {
            cJSON_AddStringToObject(item, "scope_kind", record->scope_kind);
        }
        if (record->analysis_focus[0]) {
            cJSON_AddStringToObject(item, "analysis_focus", record->analysis_focus);
        }
        cJSON_AddStringToObject(item, "status", record->status);
        cJSON_AddNumberToObject(item, "started_ms", record->started_ms);
        cJSON_AddNumberToObject(item, "finished_ms", record->finished_ms);
        cJSON_AddNumberToObject(item, "elapsed_ms", record->elapsed_ms);
        cJSON_AddBoolToObject(item, "write_approved", record->write_approved);
        if (record->target_files[0]) {
            cJSON_AddStringToObject(item, "target_files", record->target_files);
        }
        if (record->blocker_kind[0]) {
            cJSON_AddStringToObject(item, "blocker_kind", record->blocker_kind);
        }
        if (record->blocker_text[0]) {
            cJSON_AddStringToObject(item, "blocker_text", record->blocker_text);
        }
        if (record->output[0]) {
            cJSON_AddStringToObject(item, "output", record->output);
        }
        cJSON_AddItemToArray(tasks, item);
    }

    cJSON_AddItemToObject(root, "coordinators", coordinators);
    cJSON_AddItemToObject(root, "tasks", tasks);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) {
        return ERR_NO_MEM;
    }

    strscpy(output, json, output_size);
    kfree(json);
    return 0;
}
