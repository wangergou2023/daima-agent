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

cJSON *agent_turn_build_tool_results(const llm_response_t *resp,
                                      const struct message *msg,
                                      char *tool_output, size_t tool_output_size,
                                      turn_exec_stats_t *stats)
{
    cJSON *content = cJSON_CreateArray();

    for (int i = 0; i < resp->call_count; i++) {
        const llm_tool_call_t *call = &resp->calls[i];
        const char *tool_input = call->input ? call->input : "{}";
        tool_runtime_result_t rt = {0};

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

        cJSON *block = cJSON_CreateObject();
        cJSON_AddStringToObject(block, "type", "tool_result");
        cJSON_AddStringToObject(block, "tool_use_id", call->id);
        cJSON_AddStringToObject(block, "content", tool_output);
        cJSON_AddItemToArray(content, block);
        kfree(rt.effective_input);
    }
    return content;
}