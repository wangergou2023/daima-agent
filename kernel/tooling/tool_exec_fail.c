/* 工具调用失败观察与 work_item 收集 */
#include "tool_exec_fail.h"
#include "tool_feedback.h"
#include "linux/printk.h"
#include "text.h"
#include "work_item.h"
#include "linux/slab.h"
#include "linux/kernel.h"
#include "cjson.h"
#include <stdio.h>
#include <string.h>

#define TOOL_FAILURE_SIGNATURE_LIMIT 16

typedef struct {
    char signatures[TOOL_FAILURE_SIGNATURE_LIMIT][192];
    int count;
} tool_failure_observer_t;

/** 记录工具调用载荷预览日志（便于调试工具调用问题）。 */
void log_tool_payload_preview(const char *phase,
                              const struct message *msg,
                              const char *tool_name,
                              const char *tool_id,
                              const char *input,
                              const char *output,
                              err_t err)
{
    char input_preview[360];
    char output_preview[360];
    text_shorten(input, input_preview, sizeof(input_preview), 320);
    text_shorten(output, output_preview, sizeof(output_preview), 320);
    pr_info("tool_payload %s chat=%s tool=%s id=%s err=%s input_len=%u input=%s output_len=%u output=%s", phase ? phase : "-", msg && msg->chat_id[0] ? msg->chat_id : "-", tool_name && tool_name[0] ? tool_name : "-", tool_id && tool_id[0] ? tool_id : "-", err_name(err), input ? (unsigned)strlen(input) : 0, input_preview[0] ? input_preview : "<empty>", output ? (unsigned)strlen(output) : 0, output_preview[0] ? output_preview : "<empty>");
}

static const char *normalize_tool_failure_output(const char *tool_name,
                                                  err_t tool_err,
                                                  const char *tool_input,
                                                  const char *tool_output)
{
    (void)tool_name;
    if (tool_err == ERR_NOT_FOUND) return "unknown_tool";
    if (tool_input && strcmp(tool_input, "{}") == 0) return "empty_input";
    if (tool_output && (strstr(tool_output, "缺少 'path'") || strstr(tool_output, "missing 'path'"))) return "missing_path";
    if (tool_output && (strstr(tool_output, "缺少 'content'") || strstr(tool_output, "missing 'content'"))) return "missing_content";
    if (tool_output && (strstr(tool_output, "只允许修改当前工作目录") || strstr(tool_output, "current workspace only"))) return "path_not_allowed";
    if (tool_output && strstr(tool_output, "dangerous_command_blocked")) return "dangerous_command_blocked";
    if (tool_output && strstr(tool_output, "Timeout")) return "timeout";
    return err_name(tool_err);
}

static bool observer_seen_signature(tool_failure_observer_t *observer, const char *signature)
{
    if (!observer || !signature || !signature[0]) return false;
    for (int i = 0; i < observer->count; i++)
        if (strcmp(observer->signatures[i], signature) == 0) return true;
    if (observer->count < TOOL_FAILURE_SIGNATURE_LIMIT) {
        strscpy(observer->signatures[observer->count], signature, sizeof(observer->signatures[observer->count]));
        observer->count++;
    }
    return false;
}

static const char *priority_for_tool_failure(const char *tool_name, err_t tool_err, const char *normalized)
{
    (void)tool_name;
    if (tool_err == ERR_NOT_FOUND) return "P1";
    if (normalized && (strcmp(normalized, "empty_input") == 0 ||
                       strcmp(normalized, "missing_path") == 0 ||
                       strcmp(normalized, "missing_content") == 0))
        return "P1";
    return "P2";
}

static void add_string_array_item(cJSON *obj, const char *key, const char *value)
{
    cJSON *arr = cJSON_CreateArray();
    if (!arr) return;
    if (value && value[0]) cJSON_AddItemToArray(arr, cJSON_CreateString(value));
    cJSON_AddItemToObject(obj, key, arr);
}

/** 收集工具失败的工作项：去重 → 构造 title/description/evidence → 存入 work_item 存储。
 *  同签名失败只记录一次（static observer 去重）。 */
void collect_tool_failure_work_item(const struct message *msg,
                                     const char *tool_name,
                                     const char *tool_input,
                                     const char *tool_output,
                                     err_t tool_err)
{
    if (!tool_name || tool_err == 0) return;

    static tool_failure_observer_t observer;

    const char *normalized = normalize_tool_failure_output(tool_name, tool_err, tool_input, tool_output);
    char signature[192];
    snprintf(signature, sizeof(signature), "tool:%s|err:%s|output:%s",
             tool_name, err_name(tool_err), normalized ? normalized : "unknown");
    if (observer_seen_signature(&observer, signature)) return;

    char input_preview[256], output_preview[256];
    text_shorten(tool_input, input_preview, sizeof(input_preview), 220);
    text_shorten(tool_output, output_preview, sizeof(output_preview), 220);

    char title[256];
    if (tool_err == ERR_NOT_FOUND)
        snprintf(title, sizeof(title), "Model invoked unknown tool %s", tool_name);
    else if (tool_input && strcmp(tool_input, "{}") == 0)
        snprintf(title, sizeof(title), "Tool %s failed because it received empty input", tool_name);
    else
        snprintf(title, sizeof(title), "Tool invocation failed: %s", tool_name);

    char desc[768];
    snprintf(desc, sizeof(desc), "Tool %s failed with error %s. input=%s output=%s",
             tool_name, err_name(tool_err), input_preview, output_preview);

    cJSON *input = cJSON_CreateObject();
    if (!input) return;
    cJSON_AddStringToObject(input, "type", "defect");
    cJSON_AddStringToObject(input, "source", "log");
    cJSON_AddStringToObject(input, "title", title);
    cJSON_AddStringToObject(input, "description", desc);
    cJSON_AddStringToObject(input, "expected", "Tool invocations should use an existing tool name and valid parameters required by the schema.");
    cJSON_AddStringToObject(input, "actual", desc);
    cJSON_AddStringToObject(input, "status", "triaged");
    cJSON_AddStringToObject(input, "priority", priority_for_tool_failure(tool_name, tool_err, normalized));
    cJSON_AddStringToObject(input, "error_signature", signature);

    cJSON *evidence = cJSON_CreateObject();
    if (evidence) {
        cJSON_AddStringToObject(evidence, "session_id", msg ? msg->chat_id : "");
        cJSON_AddStringToObject(evidence, "issue_url", "");
        char log_line[640];
        snprintf(log_line, sizeof(log_line), "Tool %s failed: %s input=%s output=%s",
                 tool_name, err_name(tool_err), input_preview, output_preview);
        add_string_array_item(evidence, "logs", log_line);
        add_string_array_item(evidence, "files", "");
        add_string_array_item(evidence, "commands", "");
        cJSON *tool_calls = cJSON_CreateArray();
        cJSON *call = cJSON_CreateObject();
        if (tool_calls && call) {
            cJSON_AddStringToObject(call, "tool", tool_name);
            cJSON_AddStringToObject(call, "input", tool_input ? tool_input : "{}");
            cJSON_AddStringToObject(call, "error", err_name(tool_err));
            cJSON_AddStringToObject(call, "output", tool_output ? tool_output : "");
            cJSON_AddItemToArray(tool_calls, call);
            cJSON_AddItemToObject(evidence, "tool_calls", tool_calls);
        } else {
            cJSON_Delete(tool_calls);
            cJSON_Delete(call);
        }
        cJSON_AddItemToObject(input, "evidence", evidence);
    }

    cJSON *item = NULL;
    err_t err = work_item_store_collect_structured(input, &item);
    if (err == 0) {
        const char *id = cJSON_GetStringValue(cJSON_GetObjectItem(item, "id"));
        pr_info("Collected work item for tool failure: %s (%s)", id ? id : "-", signature);
    } else {
        pr_warn("Collect work item for tool failure failed: %s", err_name(err));
    }
    cJSON_Delete(item);
    cJSON_Delete(input);
}
