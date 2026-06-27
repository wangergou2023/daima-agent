/* delegate_task routing helpers */
#include "drivers/tool/tool_delegate_route.h"

#include "drivers/tool/tool_delegate_types.h"
#include "drivers/tool/tool_delegate_repo_batch.h"
#include "drivers/tool/tool_delegate_request.h"
#include "drivers/tool/tool_delegate_response.h"
#include "drivers/tool/tool_delegate_runtime.h"
#include "drivers/tool/tool_delegate_snapshot.h"
#include "drivers/tool/tool_delegate_subagent.h"
#include "drivers/tool/tool_delegate_sync.h"
#include "delegate/delegate_task_store.h"
#include "drivers/tool/tool_runtime.h"

#include "linux/kernel.h"

#include <stdio.h>
#include <string.h>

static err_t continue_background_subagent(const delegate_request_t *req,
                                          char *output,
                                          size_t output_size)
{
    delegate_task_record_t record;
    err_t err = delegate_task_store_poll(req->task_id, &record);
    if (err != 0) {
        snprintf(output, output_size, "delegate_task: task_id not found: %s", req->task_id);
        return err;
    }

    const char *status = "running";
    if (record.status == DELEGATE_TASK_DONE) {
        status = "done";
    } else if (record.status == DELEGATE_TASK_FAILED) {
        status = "failed";
    }

    return tool_delegate_write_json_response(output, output_size, record.task_id, record.session_id, status, NULL,
                                             record.subagent_type, record.description,
                                             record.model,
                                             record.output[0] ? record.output : "");
}

err_t tool_delegate_execute(const char *input_json,
                            char *output,
                            size_t output_size)
{
    delegate_request_t req;
    delegate_request_t batch_req;
    const struct message *current_msg = tool_runtime_current_message();
    const char *parent_chat_id = current_msg ? current_msg->chat_id : "";
    err_t err = tool_delegate_parse_request(input_json, &req, output, output_size);
    if (err != 0) {
        return err;
    }

    if (req.action[0]) {
        const char *scope = req.scope[0] ? req.scope : "parent";
        if (strcmp(req.action, "list") == 0 && strcmp(scope, "parent") == 0) {
            return tool_delegate_render_parent_registry_view(parent_chat_id, output, output_size);
        }
        snprintf(output, output_size, "delegate_task: unsupported scope '%s' for action '%s'",
                 scope, req.action);
        return ERR_INVALID_ARG;
    }

    delegate_subagent_kind_t kind = tool_delegate_parse_subagent_kind(req.subagent_type);
    if (req.coordinator_id[0]) {
        return tool_delegate_render_background_coordinator_snapshot(req.coordinator_id, output, output_size);
    }
    if (req.task_id[0]) {
        return continue_background_subagent(&req, output, output_size);
    }

    if (tool_delegate_should_expand_repo_root_overview_batch(&req)) {
        tool_delegate_fill_repo_root_overview_batch_request(&req, &batch_req);
        if (batch_req.is_batch && batch_req.batch_count > 0) {
            pr_info("delegate repo-root overview expanded to batch: description=%s tasks=%d",
                    req.description[0] ? req.description : "-",
                    batch_req.batch_count);
            return tool_delegate_run_background_coordinator(&batch_req, parent_chat_id, output, output_size);
        }
    }

    if (req.is_batch) {
        return tool_delegate_run_background_coordinator(&req, parent_chat_id, output, output_size);
    }

    if (req.run_in_background) {
        return tool_delegate_run_background_subagent(kind, &req, "", parent_chat_id, output, output_size);
    }

    return tool_delegate_run_sync_single_subagent(kind, &req, "", "", req.coordinator_id, parent_chat_id, output, output_size);
}
