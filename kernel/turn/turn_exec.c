/* 工具执行：LLM 响应构建、工具结果组装 */
#include "turn_exec.h"
#include "tool_exec_fail.h"
#include "auto_verify.h"
#include "tool_feedback.h"
#include "tool_guard.h"
#include "linux/printk.h"
#include "text.h"
#include "drivers/tool/tool_runtime.h"
#include "drivers/tool/tool_terminal_exec.h"
#include "work_item.h"
#include "linux/slab.h"
#include "linux/kernel.h"
#include "cjson.h"
#include "turn_dispatch.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    int call_index;
    char id[64];
    char description[64];
    char prompt[2048];
    char subagent_type[24];
} delegate_batch_member_t;

typedef struct {
    int count;
    int primary_index;
    delegate_batch_member_t members[MAX_TOOL_CALLS];
} delegate_batch_group_t;

cJSON *agent_turn_build_assistant_content(const llm_response_t *resp)
{
    cJSON *content = cJSON_CreateArray();

    if (resp->reasoning_content && resp->reasoning_content_len > 0) {
        cJSON *block = cJSON_CreateObject();
        cJSON_AddStringToObject(block, "type", "reasoning");
        cJSON_AddStringToObject(block, "text", resp->reasoning_content);
        cJSON_AddItemToArray(content, block);
    }

    if (resp->text && resp->text_len > 0) {
        cJSON *block = cJSON_CreateObject();
        cJSON_AddStringToObject(block, "type", "text");
        cJSON_AddStringToObject(block, "text", resp->text);
        cJSON_AddItemToArray(content, block);
    }

    for (int i = 0; i < resp->call_count; i++) {
        const llm_tool_call_t *call = &resp->calls[i];
        cJSON *block = cJSON_CreateObject();
        cJSON_AddStringToObject(block, "type", "tool_use");
        cJSON_AddStringToObject(block, "id", call->id);
        cJSON_AddStringToObject(block, "name", call->name);
        cJSON *input = cJSON_Parse(call->input);
        cJSON_AddItemToObject(block, "input", input ? input : cJSON_CreateObject());
        if (!input) cJSON_AddItemToObject(block, "input", cJSON_CreateObject());
        cJSON_AddItemToArray(content, block);
    }
    return content;
}

char *agent_turn_generate_forced_final_response(const char *system_prompt,
                                                 cJSON *messages, const char *reason)
{
    if (!system_prompt || !messages) return NULL;

    cJSON *user_msg = cJSON_CreateObject();
    if (!user_msg) return NULL;
    cJSON_AddStringToObject(user_msg, "role", "user");

    const char *prefix = reason && reason[0] ? reason : "工具调用轮次已达上限。";
    const char *suffix = "不要再调用任何工具。请仅基于当前已有的对话和工具结果，直接输出最终答复。";
    size_t sz = strlen(prefix) + strlen(suffix) + 2;
    char *content = kzalloc(sz, GFP_KERNEL);
    if (!content) { cJSON_Delete(user_msg); return NULL; }
    snprintf(content, sz, "%s%s", prefix, suffix);
    cJSON_AddStringToObject(user_msg, "content", content);
    kfree(content);
    cJSON_AddItemToArray(messages, user_msg);

    llm_response_t resp;
    memset(&resp, 0, sizeof(resp));
    err_t err = llm_chat_tools(system_prompt, messages, NULL, &resp);
    if (err != 0 || resp.tool_use || !resp.text || !resp.text[0]) {
        llm_response_free(&resp);
        return NULL;
    }
    char *final_text = strdup(resp.text);
    llm_response_free(&resp);
    return final_text;
}

static bool is_terminal_verification(const char *command)
{
    static const char *kw[] = {"cmake --build","ctest","make ","ninja ","pytest","npm test","go test","cargo test","cargo check"};
    for (size_t i = 0; i < sizeof(kw)/sizeof(kw[0]); i++)
        if (command && strstr(command, kw[i])) return true;
    return false;
}

static bool parse_delegate_batch_candidate(const llm_tool_call_t *call,
                                           delegate_batch_member_t *out)
{
    if (!call || !out || strcmp(call->name, "delegate_task") != 0) {
        return false;
    }

    cJSON *root = cJSON_Parse(call->input ? call->input : "{}");
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }

    const char *task_id = cJSON_GetStringValue(cJSON_GetObjectItem(root, "task_id"));
    const char *coordinator_id = cJSON_GetStringValue(cJSON_GetObjectItem(root, "coordinator_id"));
    const char *subagent_type = cJSON_GetStringValue(cJSON_GetObjectItem(root, "subagent_type"));
    const char *prompt = cJSON_GetStringValue(cJSON_GetObjectItem(root, "prompt"));
    const char *description = cJSON_GetStringValue(cJSON_GetObjectItem(root, "description"));

    bool ok = (!task_id || !task_id[0]) &&
              (!coordinator_id || !coordinator_id[0]) &&
              subagent_type && subagent_type[0] &&
              prompt && prompt[0];

    if (ok) {
        memset(out, 0, sizeof(*out));
        out->call_index = -1;
        strscpy(out->id, call->id, sizeof(out->id));
        strscpy(out->subagent_type, subagent_type, sizeof(out->subagent_type));
        strscpy(out->prompt, prompt, sizeof(out->prompt));
        strscpy(out->description,
                description && description[0] ? description : subagent_type,
                sizeof(out->description));
    }

    cJSON_Delete(root);
    return ok;
}

static bool collect_delegate_batch_group(const llm_response_t *resp,
                                         delegate_batch_group_t *out)
{
    if (!resp || !out) {
        return false;
    }

    memset(out, 0, sizeof(*out));
    out->primary_index = -1;
    for (int i = 0; i < resp->call_count && out->count < MAX_TOOL_CALLS; i++) {
        delegate_batch_member_t member;
        if (!parse_delegate_batch_candidate(&resp->calls[i], &member)) {
            continue;
        }
        member.call_index = i;
        if (out->primary_index < 0) {
            out->primary_index = i;
        }
        out->members[out->count++] = member;
    }

    return out->count >= 2 && out->primary_index >= 0;
}

static char *build_delegate_batch_input(const delegate_batch_group_t *group)
{
    if (!group || group->count < 2) {
        return NULL;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *tasks = cJSON_CreateArray();
    if (!root || !tasks) {
        cJSON_Delete(root);
        cJSON_Delete(tasks);
        return NULL;
    }

    for (int i = 0; i < group->count; i++) {
        const delegate_batch_member_t *member = &group->members[i];
        cJSON *item = cJSON_CreateObject();
        if (!item) {
            continue;
        }
        cJSON_AddStringToObject(item, "subagent_type", member->subagent_type);
        cJSON_AddStringToObject(item, "description", member->description);
        cJSON_AddStringToObject(item, "prompt", member->prompt);
        cJSON_AddItemToArray(tasks, item);
    }
    cJSON_AddItemToObject(root, "tasks", tasks);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

static char *extract_coordinator_id_from_output(const char *tool_output)
{
    if (!tool_output || !tool_output[0]) {
        return NULL;
    }
    cJSON *root = cJSON_Parse(tool_output);
    if (!root) {
        return NULL;
    }
    const char *coordinator_id = cJSON_GetStringValue(cJSON_GetObjectItem(root, "coordinator_id"));
    char *result = coordinator_id && coordinator_id[0] ? strdup(coordinator_id) : NULL;
    cJSON_Delete(root);
    return result;
}

static char *build_merged_delegate_result(const char *coordinator_id,
                                          const char *primary_tool_use_id,
                                          const char *subagent_type)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return NULL;
    }
    cJSON_AddStringToObject(root, "status", "merged_into_batch");
    if (coordinator_id && coordinator_id[0]) {
        cJSON_AddStringToObject(root, "coordinator_id", coordinator_id);
    }
    if (primary_tool_use_id && primary_tool_use_id[0]) {
        cJSON_AddStringToObject(root, "primary_tool_use_id", primary_tool_use_id);
    }
    if (subagent_type && subagent_type[0]) {
        cJSON_AddStringToObject(root, "subagent_type", subagent_type);
    }
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

static void maybe_mark_background_delegate_started(turn_exec_stats_t *stats,
                                                   const char *tool_name,
                                                   const char *tool_output)
{
    if (!stats || !tool_name || strcmp(tool_name, "delegate_task") != 0 || stats->background_delegate_started) {
        return;
    }
    char *coordinator_id = extract_coordinator_id_from_output(tool_output);
    if (!coordinator_id || !coordinator_id[0]) {
        free(coordinator_id);
        return;
    }
    stats->background_delegate_started = true;
    snprintf(stats->background_delegate_reply,
             sizeof(stats->background_delegate_reply),
             "已启动后台子任务，coordinator_id=%s。后续进度和完成结果将通过实时事件返回。",
             coordinator_id);
    free(coordinator_id);
}

static void append_tool_result_block(cJSON *content,
                                     const char *tool_use_id,
                                     const char *result_text)
{
    cJSON *block = cJSON_CreateObject();
    if (!content || !block) {
        cJSON_Delete(block);
        return;
    }
    cJSON_AddStringToObject(block, "type", "tool_result");
    cJSON_AddStringToObject(block, "tool_use_id", tool_use_id ? tool_use_id : "");
    cJSON_AddStringToObject(block, "content", result_text ? result_text : "");
    cJSON_AddItemToArray(content, block);
}

cJSON *agent_turn_build_tool_results(const llm_response_t *resp,
                                      const struct message *msg,
                                      char *tool_output, size_t tool_output_size,
                                      turn_exec_stats_t *stats)
{
    cJSON *content = cJSON_CreateArray();
    delegate_batch_group_t batch_group;
    bool has_delegate_batch = collect_delegate_batch_group(resp, &batch_group);
    char *batch_output = NULL;
    char *batch_coordinator_id = NULL;
    err_t batch_err = 0;
    bool batch_executed = false;

    for (int i = 0; i < resp->call_count; i++) {
        const llm_tool_call_t *call = &resp->calls[i];
        const char *tool_input = call->input ? call->input : "{}";
        tool_runtime_result_t rt = {0};

        if (has_delegate_batch && strcmp(call->name, "delegate_task") == 0) {
            bool is_batch_member = false;
            const delegate_batch_member_t *member = NULL;
            for (int j = 0; j < batch_group.count; j++) {
                if (batch_group.members[j].call_index == i) {
                    is_batch_member = true;
                    member = &batch_group.members[j];
                    break;
                }
            }
            if (is_batch_member && i != batch_group.primary_index) {
                char *merged = build_merged_delegate_result(batch_coordinator_id,
                                                            resp->calls[batch_group.primary_index].id,
                                                            member ? member->subagent_type : "");
                append_tool_result_block(content, call->id, merged ? merged : "{\"status\":\"merged_into_batch\"}");
                kfree(merged);
                continue;
            }
            if (is_batch_member && i == batch_group.primary_index) {
                char *batch_input = build_delegate_batch_input(&batch_group);
                llm_tool_call_t merged_call = *call;
                merged_call.input = batch_input;
                log_tool_payload_preview("before_runtime", msg, "delegate_task", call->id, batch_input, NULL, 0);
                batch_err = tool_runtime_execute_call(&merged_call, msg, tool_output, tool_output_size, &rt);
                batch_executed = true;
                if (rt.effective_input) tool_input = rt.effective_input;
                log_tool_payload_preview(rt.effective_input ? "after_runtime_patched" : "after_runtime",
                                         msg, "delegate_task", call->id, tool_input, tool_output, batch_err);
                record_turn_side_effects(stats, "delegate_task", tool_input);
                agent_tool_feedback_send_activity(msg, "delegate_task", tool_input, tool_output, batch_err, rt.elapsed_ms);
                collect_tool_failure_work_item(msg, "delegate_task", tool_input, tool_output, batch_err);
                if (batch_err == 0) {
                    batch_coordinator_id = extract_coordinator_id_from_output(tool_output);
                    batch_output = strdup(tool_output);
                    if (stats) {
                        stats->background_delegate_started = true;
                        snprintf(stats->background_delegate_reply,
                                 sizeof(stats->background_delegate_reply),
                                 "已启动 %d 个后台子任务，coordinator_id=%s。后续进度和完成结果将通过实时事件返回。",
                                 batch_group.count,
                                 batch_coordinator_id && batch_coordinator_id[0] ? batch_coordinator_id : "unknown");
                    }
                    pr_info("delegate batch merged: primary=%s merged_calls=%d coordinator=%s",
                            call->id,
                            batch_group.count,
                            batch_coordinator_id ? batch_coordinator_id : "<missing>");
                }
                text_sanitize_utf8_json(tool_output);
                append_tool_result_block(content, call->id, tool_output);
                kfree(rt.effective_input);
                kfree(batch_input);
                continue;
            }
        }

        log_tool_payload_preview("before_runtime", msg, call->name, call->id, tool_input, NULL, 0);

        err_t tool_err = tool_runtime_execute_call(call, msg,
                                                    tool_output, tool_output_size, &rt);
        if (rt.effective_input) tool_input = rt.effective_input;

        log_tool_payload_preview(rt.effective_input ? "after_runtime_patched" : "after_runtime",
                                 msg, call->name, call->id, tool_input, tool_output, tool_err);

        record_turn_side_effects(stats, call->name, tool_input);
        agent_tool_feedback_send_activity(msg, call->name, tool_input, tool_output, tool_err, rt.elapsed_ms);
        collect_tool_failure_work_item(msg, call->name, tool_input, tool_output, tool_err);

        if (agent_tool_protocol_failure_should_stop(call->name, tool_input, tool_output, tool_err)) {
            stats->unrecoverable_tool_protocol_error = true;
            snprintf(stats->tool_protocol_error_reason, sizeof(stats->tool_protocol_error_reason),
                     "工具 %s 收到无效协议参数 input=%s", call->name, tool_input ? tool_input : "{}");
        }

        if (strcmp(call->name, "terminal") == 0 && tool_input && is_terminal_verification(tool_input)) {
            cJSON *tr = cJSON_Parse(tool_output);
            if (tr) {
                cJSON *ec = cJSON_GetObjectItem(tr, "exit_code");
                cJSON *to = cJSON_GetObjectItem(tr, "timed_out");
                if ((ec && cJSON_IsNumber(ec) && ec->valueint != 0) ||
                    (to && cJSON_IsBool(to) && cJSON_IsTrue(to))) {
                    char title[256];
                    snprintf(title, sizeof(title), "验证命令失败: %.200s", tool_input);
                    work_item_store_collect("defect", "test", title, tool_output);
                }
                cJSON_Delete(tr);
            }
        } else if (strcmp(call->name, "webfetch") == 0 && tool_err != 0) {
            cJSON *wf = cJSON_Parse(tool_input);
            const char *url = NULL;
            if (wf) url = cJSON_GetStringValue(cJSON_GetObjectItem(wf, "url"));
            if (url && url[0]) {
                char title[256];
                snprintf(title, sizeof(title), "webfetch 失败: %.200s", url);
                work_item_store_collect("defect", "test", title, tool_output);
            }
            cJSON_Delete(wf);
        }

        if (tool_err == 0)
            pr_info("Tool %s result: %d bytes", call->name, (int)strlen(tool_output));
        else {
            char ip[240], op[240];
            text_shorten(tool_input, ip, sizeof(ip), 220);
            text_shorten(tool_output, op, sizeof(op), 220);
            pr_warn("Tool %s failed: %s input=%s output=%s", call->name, err_name(tool_err), ip, op);
        }

        text_sanitize_utf8_json(tool_output);
        maybe_mark_background_delegate_started(stats, call->name, tool_output);

        append_tool_result_block(content, call->id, tool_output);
        kfree(rt.effective_input);
    }
    if (batch_executed && batch_output) {
        kfree(batch_output);
    }
    kfree(batch_coordinator_id);
    return content;
}
