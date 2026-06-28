/* delegate_task preflight execution helpers */
#include "drivers/tool/tool_delegate_preflight.h"

#include <string.h>

#include "drivers/channel/gateway/ws_server.h"
#include "drivers/tool/tool_delegate_protocol.h"
#include "drivers/tool/tool_runtime.h"
#include "delegate/delegate_task_store.h"
#include "linux/kernel.h"

err_t tool_delegate_execute_preflight_tool(const delegate_request_t *req,
                                           const struct message *msg,
                                           cJSON *messages,
                                           const char *task_id,
                                           const char *session_id,
                                           const char *coordinator_id,
                                           const char *parent_chat_id,
                                           const char *subagent_type,
                                           const char *description,
                                           const char *scope_path,
                                           const char *scope_kind,
                                           const char *analysis_focus,
                                           char *final_summary,
                                           size_t final_summary_size,
                                           bool *out_blocked)
{
    llm_tool_call_t call = {0};
    tool_runtime_result_t rt = {0};
    char tool_output[4096];
    char rendered_summary[2048];
    err_t err;
    if (out_blocked) {
        *out_blocked = false;
    }
    if (!req || !msg || !messages || !final_summary || final_summary_size == 0) {
        return ERR_INVALID_ARG;
    }
    final_summary[0] = '\0';

    if (!req->preflight_tool.tool_name[0] || !req->preflight_tool.input_json[0]) {
        return 0;
    }

    strscpy(call.id, "delegate_preflight", sizeof(call.id));
    strscpy(call.name, req->preflight_tool.tool_name, sizeof(call.name));
    call.input = (char *)req->preflight_tool.input_json;
    call.input_len = strlen(call.input);
    tool_output[0] = '\0';
    rendered_summary[0] = '\0';

    err = tool_runtime_execute_call(&call, msg, tool_output, sizeof(tool_output), &rt);

    if (tool_delegate_try_fast_local_json(subagent_type,
                                          description,
                                          tool_output,
                                          rendered_summary,
                                          sizeof(rendered_summary)) ||
        tool_delegate_finalize_result_json(subagent_type,
                                           description,
                                           tool_output,
                                           rendered_summary,
                                           sizeof(rendered_summary))) {
        strscpy(final_summary, rendered_summary, final_summary_size);
    } else {
        tool_delegate_build_safe_output_text(tool_output, "", false, false, final_summary, final_summary_size);
    }

    if (task_id && task_id[0]) {
        char step_detail[256];
        snprintf(step_detail,
                 sizeof(step_detail),
                 "preflight %s",
                 req->preflight_tool.tool_name[0] ? req->preflight_tool.tool_name : "tool");
        delegate_task_store_append_session_step(task_id,
                                                "tool",
                                                step_detail,
                                                final_summary);
    }

    if ((strstr(tool_output, "\"status\":\"sudo_password_required\"") != NULL) ||
        (strstr(tool_output, "\"error\":\"sudo_password_cancelled\"") != NULL)) {
        if (task_id && task_id[0]) {
            delegate_task_store_mark_blocked(task_id,
                                             "permission",
                                             final_summary,
                                             tool_output);
        }
        if (parent_chat_id && parent_chat_id[0]) {
            ws_server_send_subagent_event(parent_chat_id,
                                          "subagent_blocked",
                                          task_id,
                                          session_id,
                                          coordinator_id,
                                          0,
                                          subagent_type,
                                          "blocked",
                                          description,
                                          final_summary,
                                          tool_output,
                                          final_summary,
                                          "",
                                          scope_path,
                                          scope_kind,
                                          analysis_focus,
                                          "permission",
                                          final_summary,
                                          "task");
        }
        if (out_blocked) {
            *out_blocked = true;
        }
        if (!req->preflight_tool.continue_on_error) {
            return 0;
        }
    } else if (task_id && task_id[0]) {
        delegate_task_store_clear_blocked(task_id);
    }

    return err;
}
