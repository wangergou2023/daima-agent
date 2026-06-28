#include "drivers/tool/tool_delegate_sync.h"

#include "autoconf.h"
#include "cjson.h"
#include "drivers/channel/gateway/ws_server.h"
#include "drivers/tool/tool_delegate_finalize.h"
#include "drivers/llm/llm_proxy.h"
#include "drivers/tool/tool_bus_view.h"
#include "drivers/tool/tool_delegate_dependency.h"
#include "drivers/tool/tool_delegate_overview.h"
#include "drivers/tool/tool_delegate_preflight.h"
#include "drivers/tool/tool_delegate_protocol.h"
#include "drivers/tool/tool_delegate_repo_batch.h"
#include "drivers/tool/tool_delegate_response.h"
#include "drivers/tool/tool_delegate_scope.h"
#include "drivers/tool/tool_delegate_subagent.h"
#include "drivers/tool/tool_delegate.h"
#include "drivers/tool/tool_files.h"
#include "drivers/tool/tool_runtime.h"
#include "linux/kernel.h"
#include "linux/printk.h"
#include "linux/slab.h"
#include "turn_run.h"

#define DELEGATE_RESULT_JSON_MAX 3072

static void append_user_message(cJSON *messages, const char *prompt)
{
    cJSON *um = cJSON_CreateObject();
    cJSON_AddStringToObject(um, "role", "user");
    cJSON_AddStringToObject(um, "content", prompt ? prompt : "");
    cJSON_AddItemToArray(messages, um);
}

static int sync_subagent_tool_budget(delegate_subagent_kind_t kind, const delegate_request_t *req)
{
    if (kind != DELEGATE_SUBAGENT_EXPLORE || !req) {
        return 0;
    }
    if (tool_delegate_request_is_bounded_explore_overview(req)) {
        return 4;
    }
    return 0;
}

static bool delegate_request_needs_scope_listing_preflight(delegate_subagent_kind_t kind,
                                                           const delegate_request_t *req)
{
    if (kind != DELEGATE_SUBAGENT_EXPLORE || !req) {
        return false;
    }
    if (req->preflight_tool.tool_name[0]) {
        return false;
    }
    if (!req->target_path[0] || !tool_delegate_file_is_directory(req->target_path)) {
        return false;
    }
    return true;
}

static void fill_scope_listing_preflight(delegate_request_t *dst, const delegate_request_t *src)
{
    if (!dst || !src) {
        return;
    }
    memcpy(dst, src, sizeof(*dst));
    strscpy(dst->preflight_tool.tool_name, "files", sizeof(dst->preflight_tool.tool_name));
    snprintf(dst->preflight_tool.input_json,
             sizeof(dst->preflight_tool.input_json),
             "{\"action\":\"list\",\"path\":\"%s\"}",
             src->target_path);
    dst->preflight_tool.continue_on_error = true;
}

static void persist_delegate_shortcut_session(const char *session_id,
                                              const char *user_prompt,
                                              const char *assistant_text,
                                              const char *reasoning_text)
{
    tool_delegate_persist_turn_session(session_id, user_prompt, assistant_text, reasoning_text);
}

err_t tool_delegate_run_sync_single_subagent(delegate_subagent_kind_t kind,
                                             const delegate_request_t *req,
                                             const char *task_id,
                                             const char *session_id,
                                             const char *coordinator_id,
                                             const char *parent_chat_id,
                                             char *output,
                                             size_t output_size)
{
    char dependency_summary[DELEGATE_RESULT_JSON_MAX];
    char visible_output[DELEGATE_RESULT_JSON_MAX];
    char prepared_prompt[READ_FILE_MAX_CHARS + 4096];
    char scope_path[DELEGATE_TASK_SCOPE_PATH_LEN];
    char scope_kind[DELEGATE_TASK_SCOPE_KIND_LEN];
    char analysis_focus[DELEGATE_TASK_ANALYSIS_FOCUS_LEN];
    char preflight_summary[DELEGATE_RESULT_JSON_MAX];
    bool disable_tools = false;
    bool preflight_blocked = false;

    tool_delegate_infer_scope_metadata(req,
                                       scope_path,
                                       sizeof(scope_path),
                                       scope_kind,
                                       sizeof(scope_kind),
                                       analysis_focus,
                                       sizeof(analysis_focus));

    pr_info("delegate_sync preflight state: task_id=%s session_id=%s subagent=%s preflight_tool=%s continue_on_error=%d",
            task_id && task_id[0] ? task_id : "-",
            session_id && session_id[0] ? session_id : "-",
            req->subagent_type[0] ? req->subagent_type : "-",
            req->preflight_tool.tool_name[0] ? req->preflight_tool.tool_name : "-",
            req->preflight_tool.continue_on_error ? 1 : 0);

    if (tool_delegate_try_render_local_dependency_merge(req,
                                                        coordinator_id,
                                                        dependency_summary,
                                                        sizeof(dependency_summary))) {
        persist_delegate_shortcut_session(session_id && session_id[0] ? session_id : "",
                                          req->prompt[0] ? req->prompt : req->description,
                                          dependency_summary,
                                          "dependency merge shortcut");
        if (task_id && task_id[0]) {
            delegate_task_store_append_session_step(task_id,
                                                    "tool",
                                                    "dependency merge shortcut",
                                                    dependency_summary);
        }
        if (parent_chat_id && parent_chat_id[0]) {
            ws_server_send_subagent_event(parent_chat_id,
                                          "subagent_start",
                                          task_id,
                                          session_id,
                                          coordinator_id,
                                          0,
                                          req->subagent_type,
                                          "running",
                                          req->description,
                                          "dependency_merge",
                                          dependency_summary,
                                          tool_delegate_visible_output_or_fallback(dependency_summary,
                                                                                   visible_output,
                                                                                   sizeof(visible_output)),
                                          "",
                                          scope_path,
                                          scope_kind,
                                          analysis_focus,
                                          "",
                                          "",
                                          "task");
            ws_server_send_subagent_event(parent_chat_id,
                                          "subagent_done",
                                          task_id,
                                          session_id,
                                          coordinator_id,
                                          0,
                                          req->subagent_type,
                                          "done",
                                          req->description,
                                          "dependency_merge",
                                          dependency_summary,
                                          tool_delegate_visible_output_or_fallback(dependency_summary,
                                                                                   visible_output,
                                                                                   sizeof(visible_output)),
                                          "",
                                          scope_path,
                                          scope_kind,
                                          analysis_focus,
                                          "",
                                          "",
                                          "task");
        }
        return tool_delegate_write_json_response(output, output_size, NULL, session_id, "done", "sync_final",
                                                 req->subagent_type, req->description,
                                                 tool_delegate_subagent_model_for_kind(kind), dependency_summary);
    }

    tool_delegate_prepare_subagent_prompt(req->subagent_type,
                                          req->description,
                                          req->target_path,
                                          req->prompt,
                                          prepared_prompt,
                                          sizeof(prepared_prompt),
                                          &disable_tools);
    if (req->depends_on[0] && coordinator_id && coordinator_id[0]) {
        char dependency_context[DELEGATE_RESULT_JSON_MAX];
        dependency_context[0] = '\0';
        if (tool_delegate_append_dependency_results_context(coordinator_id,
                                                            req->depends_on,
                                                            dependency_context,
                                                            sizeof(dependency_context))) {
            size_t used = strlen(prepared_prompt);
            if (used + strlen(dependency_context) + 64 < sizeof(prepared_prompt)) {
                strlcat(prepared_prompt, "\n\nCompleted upstream dependency findings:\n", sizeof(prepared_prompt));
                strlcat(prepared_prompt, dependency_context, sizeof(prepared_prompt));
            }
        }
    }

    cJSON *messages = cJSON_CreateArray();
    if (!messages) {
        snprintf(output, output_size, "delegate_task: no memory");
        return ERR_NO_MEM;
    }
    append_user_message(messages, prepared_prompt);

    const char *tools_json = disable_tools
        ? NULL
        : tool_bus_tools_json_for_channel_without_delegate("websocket");
    struct message msg;
    memset(&msg, 0, sizeof(msg));
    strscpy(msg.channel, "websocket", sizeof(msg.channel));
    if (session_id && session_id[0]) {
        strscpy(msg.chat_id, session_id, sizeof(msg.chat_id));
    } else {
        snprintf(msg.chat_id, sizeof(msg.chat_id), "delegate_sync_%d", tool_delegate_next_seq());
    }
    strscpy(msg.source, MSG_SOURCE_DELEGATE, sizeof(msg.source));
    msg.content = prepared_prompt[0] ? strdup(prepared_prompt) : strdup(req->description);
    if (!msg.content) {
        cJSON_Delete(messages);
        snprintf(output, output_size, "delegate_task: no memory");
        return ERR_NO_MEM;
    }

    char *final_text = NULL;
    char *reasoning_text = NULL;
    int iteration = 0;
    bool tool_budget_exhausted = false;
    bool cancelled = false;
    int max_tool_iterations = sync_subagent_tool_budget(kind, req);

    if (parent_chat_id && parent_chat_id[0]) {
        ws_server_send_subagent_event(parent_chat_id,
                                      "subagent_start",
                                      task_id,
                                      msg.chat_id,
                                      coordinator_id,
                                      0,
                                      req->subagent_type,
                                      "running",
                                      req->description,
                                      max_tool_iterations > 0 ? "bounded" : "unbounded",
                                      "",
                                      "",
                                      "",
                                      scope_path,
                                      scope_kind,
                                      analysis_focus,
                                      "",
                                      "",
                                      "task");
    }

    delegate_request_t scoped_req_storage;
    const delegate_request_t *effective_req = req;
    if (delegate_request_needs_scope_listing_preflight(kind, req)) {
        fill_scope_listing_preflight(&scoped_req_storage, req);
        effective_req = &scoped_req_storage;
    }

    err_t preflight_err = tool_delegate_execute_preflight_tool(effective_req,
                                                               &msg,
                                                               messages,
                                                               task_id,
                                                               msg.chat_id,
                                                               coordinator_id,
                                                               parent_chat_id,
                                                               req->subagent_type,
                                                               req->description,
                                                               scope_path,
                                                               scope_kind,
                                                               analysis_focus,
                                                               preflight_summary,
                                                               sizeof(preflight_summary),
                                                               &preflight_blocked);
    if (preflight_err != 0 && !effective_req->preflight_tool.continue_on_error) {
        cJSON_Delete(messages);
        kfree(msg.content);
        snprintf(output, output_size, "delegate_task: preflight tool failed: %s", err_name(preflight_err));
        return preflight_err;
    }
    if (preflight_blocked && !effective_req->preflight_tool.continue_on_error) {
        cJSON_Delete(messages);
        kfree(msg.content);
        return tool_delegate_write_json_response(output, output_size, NULL, msg.chat_id, "blocked", "sync_final",
                                                 req->subagent_type, req->description,
                                                 tool_delegate_subagent_model_for_kind(kind), preflight_summary);
    }

    err_t err = agent_turn_run(
        tool_delegate_subagent_prompt_prefix(kind),
        messages,
        tools_json,
        &msg,
        tool_delegate_subagent_model_for_kind(kind),
        tool_delegate_subagent_prefers_structured_output(kind),
        max_tool_iterations,
        0,
        &final_text,
        &reasoning_text,
        &iteration,
        &tool_budget_exhausted,
        &cancelled);

    kfree(msg.content);

    if (err != 0) {
        cJSON_Delete(messages);
        snprintf(output, output_size, "delegate_task: subagent failed: %s", err_name(err));
        kfree(final_text);
        kfree(reasoning_text);
        return err;
    }

    char *raw_final_text = final_text ? strdup(final_text) : NULL;
    char *raw_reasoning_text = reasoning_text ? strdup(reasoning_text) : NULL;
    tool_delegate_persist_turn_session(msg.chat_id,
                                       prepared_prompt,
                                       raw_final_text && raw_final_text[0] ? raw_final_text : final_text,
                                       raw_reasoning_text && raw_reasoning_text[0] ? raw_reasoning_text : reasoning_text);
    if (task_id && task_id[0]) {
        char session_message[256];
        if (raw_final_text && raw_final_text[0]) {
            tool_delegate_sanitize_summary_text_copy(session_message, sizeof(session_message), raw_final_text);
            if (session_message[0]) {
                delegate_task_store_append_session_message(task_id, "assistant", session_message);
            }
        }
        if (raw_reasoning_text && raw_reasoning_text[0]) {
            tool_delegate_sanitize_summary_text_copy(session_message, sizeof(session_message), raw_reasoning_text);
            if (session_message[0]) {
                delegate_task_store_append_session_message(task_id, "reasoning", session_message);
            }
        }
    }

    tool_delegate_sanitize_summary_text_inplace(final_text);
    tool_delegate_sanitize_summary_text_inplace(reasoning_text);
    err = tool_delegate_finalize_sync_response(kind,
                                               req,
                                               msg.chat_id,
                                               messages,
                                               final_text,
                                               reasoning_text,
                                               raw_final_text,
                                               raw_reasoning_text,
                                               tool_budget_exhausted,
                                               cancelled,
                                               output,
                                               output_size);

    cJSON_Delete(messages);
    kfree(raw_final_text);
    kfree(raw_reasoning_text);
    kfree(final_text);
    kfree(reasoning_text);
    return err;
}
