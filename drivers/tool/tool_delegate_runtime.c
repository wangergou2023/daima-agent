#include "drivers/tool/tool_delegate_runtime.h"
#include "drivers/tool/tool_delegate.h"
#include "drivers/tool/tool_delegate_background.h"
#include "drivers/tool/tool_delegate_dispatch.h"
#include "drivers/tool/tool_delegate_prepare.h"
#include "drivers/tool/tool_delegate_subagent.h"

#include "drivers/tool/tool_delegate_repo_batch.h"
#include "drivers/tool/tool_delegate_response.h"
#include "drivers/tool/tool_delegate_scope.h"
#include "drivers/tool/tool_delegate_snapshot.h"
#include "drivers/tool/tool_files.h"
#include "autoconf.h"
#include "linux/slab.h"

#define DELEGATE_RESULT_JSON_MAX 3072

err_t tool_delegate_run_background_coordinator(const delegate_request_t *req,
                                               const char *parent_chat_id,
                                               char *output,
                                               size_t output_size)
{
    static const char *implement_target_files_note =
        "\n\nBefore you claim completion, include one line exactly in this format:\n"
        "target_files: <comma-separated absolute or repo-relative paths you changed or intend to change>\n"
        "If you cannot determine target files yet, still include that line with your best current file set.";
    char coordinator_id[DELEGATE_COORDINATOR_ID_LEN];
    char team_run_id[DELEGATE_TEAM_RUN_ID_LEN];

    snprintf(coordinator_id, sizeof(coordinator_id), "dc_%d", tool_delegate_next_seq());
    snprintf(team_run_id, sizeof(team_run_id), "tr_%d", tool_delegate_current_seq());

    err_t err = delegate_task_store_start_coordinator(coordinator_id,
                                                      parent_chat_id,
                                                      team_run_id,
                                                      req->team_name[0] ? req->team_name : "delegate-team",
                                                      req->dispatch_mode[0] ? req->dispatch_mode : "parallel");
    if (err != 0) {
        snprintf(output, output_size, "delegate_task: failed to create coordinator: %s", err_name(err));
        return err;
    }

    int attached_count = 0;
    for (int i = 0; i < req->batch_count; i++) {
        delegate_request_t *child = kzalloc(sizeof(*child), GFP_KERNEL);
        if (!child) {
            return ERR_NO_MEM;
        }
        strscpy(child->description, req->batch_tasks[i].description, sizeof(child->description));
        strscpy(child->target_path, req->batch_tasks[i].target_path, sizeof(child->target_path));
        if (strcmp(req->batch_tasks[i].subagent_type, "implement") == 0) {
            strscpy(child->prompt, req->batch_tasks[i].prompt, sizeof(child->prompt));
            strlcat(child->prompt, implement_target_files_note, sizeof(child->prompt));
        } else {
            strscpy(child->prompt, req->batch_tasks[i].prompt, sizeof(child->prompt));
        }
        strscpy(child->subagent_type, req->batch_tasks[i].subagent_type, sizeof(child->subagent_type));
        memcpy(&child->preflight_tool,
               &req->batch_tasks[i].preflight_tool,
               sizeof(child->preflight_tool));
        child->run_in_background = true;
        tool_delegate_normalize_batch_child_request(child);

        char task_id[DELEGATE_TASK_ID_LEN];
        char session_id[32];
        char scope_path[DELEGATE_TASK_SCOPE_PATH_LEN];
        char scope_kind[DELEGATE_TASK_SCOPE_KIND_LEN];
        char analysis_focus[DELEGATE_TASK_ANALYSIS_FOCUS_LEN];

        snprintf(task_id, sizeof(task_id), "dt_%d", tool_delegate_next_seq());
        snprintf(session_id, sizeof(session_id), "delegate_sync_%d", tool_delegate_next_seq());
        tool_delegate_infer_scope_metadata(child,
                                           scope_path,
                                           sizeof(scope_path),
                                           scope_kind,
                                           sizeof(scope_kind),
                                           analysis_focus,
                                           sizeof(analysis_focus));
        err = delegate_task_store_plan(task_id,
                                       coordinator_id,
                                       session_id,
                                       child->subagent_type,
                                       req->batch_tasks[i].task_key,
                                       child->description,
                                       child->prompt,
                                       tool_delegate_subagent_model_for_kind(
                                           tool_delegate_parse_subagent_kind(child->subagent_type)),
                                       scope_path,
                                       scope_kind,
                                       analysis_focus,
                                       req->batch_tasks[i].depends_on,
                                       (const delegate_preflight_tool_view_t *)&child->preflight_tool);
        if (err != 0) {
            kfree(child);
            snprintf(output, output_size,
                     "delegate_task: failed to persist background task %d/%d: %s",
                     i + 1,
                     req->batch_count,
                     err_name(err));
            return err;
        }
        err = delegate_task_store_attach_task(coordinator_id, task_id);
        if (err != 0) {
            kfree(child);
            snprintf(output, output_size,
                     "delegate_task: failed to attach background task %d/%d: %s",
                     i + 1,
                     req->batch_count,
                     err_name(err));
            return err;
        }
        attached_count++;
        kfree(child);
    }
    if (attached_count <= 0) {
        snprintf(output, output_size, "delegate_task: no background tasks were attached");
        return ERR_FAIL;
    }
    err = tool_delegate_launch_ready_background_subagents(coordinator_id, parent_chat_id);
    if (err != 0) {
        snprintf(output, output_size, "delegate_task: failed to launch background subagents");
        return err;
    }
    return tool_delegate_render_background_coordinator_snapshot(coordinator_id, output, output_size);
}

err_t tool_delegate_run_background_subagent(delegate_subagent_kind_t kind,
                                            const delegate_request_t *req,
                                            const char *coordinator_id,
                                            const char *parent_chat_id,
                                            char *output,
                                            size_t output_size)
{
    char task_id[DELEGATE_TASK_ID_LEN];
    char session_id[32];
    char scope_path[DELEGATE_TASK_SCOPE_PATH_LEN];
    char scope_kind[DELEGATE_TASK_SCOPE_KIND_LEN];
    char analysis_focus[DELEGATE_TASK_ANALYSIS_FOCUS_LEN];

    snprintf(task_id, sizeof(task_id), "dt_%d", tool_delegate_next_seq());
    snprintf(session_id, sizeof(session_id), "delegate_sync_%d", tool_delegate_next_seq());
    tool_delegate_infer_scope_metadata(req,
                                       scope_path,
                                       sizeof(scope_path),
                                       scope_kind,
                                       sizeof(scope_kind),
                                       analysis_focus,
                                       sizeof(analysis_focus));

    err_t err = delegate_task_store_start(
        task_id,
        coordinator_id,
        session_id,
        req->subagent_type,
        "",
        req->description,
        req->prompt,
        tool_delegate_subagent_model_for_kind(kind),
        scope_path,
        scope_kind,
        analysis_focus,
        (const delegate_preflight_tool_view_t *)&req->preflight_tool);
    if (err != 0) {
        snprintf(output, output_size, "delegate_task: failed to persist background task");
        return err;
    }

    if (tool_delegate_schedule_background_subagent(kind,
                                                   req,
                                                   task_id,
                                                   session_id,
                                                   coordinator_id,
                                                   parent_chat_id) != 0) {
        delegate_task_store_fail(task_id, ERR_FAIL, "delegate_task: failed to start background worker");
        snprintf(output, output_size, "delegate_task: failed to launch background subagent");
        return ERR_FAIL;
    }

    return tool_delegate_write_json_response(output, output_size, task_id, session_id, "running", NULL,
                                             req->subagent_type, req->description,
                                             tool_delegate_subagent_model_for_kind(kind), "");
}
