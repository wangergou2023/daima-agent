#include "agent/agent_turn_exec_helpers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "agent/tool_feedback.h"
#include "agent/tool_protocol_guard.h"
#include "daima_log.h"
#include "daima_text.h"
#include "tools/tool_file_ops.h"
#include "tools/tool_runtime.h"
#include "tools/tool_terminal_exec.h"
#include "work_items/work_item_store.h"

static const char *TAG = "agent_run";

#define TOOL_FAILURE_SIGNATURE_LIMIT 16

typedef struct {
    char signatures[TOOL_FAILURE_SIGNATURE_LIMIT][192];
    int count;
} tool_failure_observer_t;

static void shorten_text(const char *src, char *dst, size_t dst_size, size_t max_len)
{
    if (!dst || dst_size == 0) return;
    dst[0] = '\0';
    if (!src || !src[0]) return;

    size_t len = strlen(src);
    if (len <= max_len || max_len + 4 >= dst_size) {
        snprintf(dst, dst_size, "%s", src);
        return;
    }

    snprintf(dst, dst_size, "%.*s...", (int)max_len, src);
}

static void log_tool_payload_preview(const char *phase,
                                     const daima_msg_t *msg,
                                     const char *tool_name,
                                     const char *tool_id,
                                     const char *input,
                                     const char *output,
                                     daima_err_t err)
{
    char input_preview[360];
    char output_preview[360];
    shorten_text(input, input_preview, sizeof(input_preview), 320);
    shorten_text(output, output_preview, sizeof(output_preview), 320);
    DAIMA_LOGI(TAG,
               "tool_payload %s chat=%s tool=%s id=%s err=%s input_len=%u input=%s output_len=%u output=%s",
               phase ? phase : "-",
               msg && msg->chat_id[0] ? msg->chat_id : "-",
               tool_name && tool_name[0] ? tool_name : "-",
               tool_id && tool_id[0] ? tool_id : "-",
               daima_err_to_name(err),
               input ? (unsigned)strlen(input) : 0,
               input_preview[0] ? input_preview : "<empty>",
               output ? (unsigned)strlen(output) : 0,
               output_preview[0] ? output_preview : "<empty>");
}

static const char *normalize_tool_failure_output(const char *tool_name,
                                                 daima_err_t tool_err,
                                                 const char *tool_input,
                                                 const char *tool_output)
{
    if (tool_err == DAIMA_ERR_NOT_FOUND) {
        return "unknown_tool";
    }
    if (tool_input && strcmp(tool_input, "{}") == 0) {
        return "empty_input";
    }
    if (tool_output && strstr(tool_output, "缺少 'path'")) {
        return "missing_path";
    }
    if (tool_output && strstr(tool_output, "缺少 'content'")) {
        return "missing_content";
    }
    if (tool_output && strstr(tool_output, "只允许修改当前工作目录")) {
        return "path_not_allowed";
    }
    if (tool_output && strstr(tool_output, "dangerous_command_blocked")) {
        return "dangerous_command_blocked";
    }
    if (tool_output && strstr(tool_output, "Timeout")) {
        return "timeout";
    }
    return daima_err_to_name(tool_err);
}

static bool observer_seen_signature(tool_failure_observer_t *observer, const char *signature)
{
    if (!observer || !signature || !signature[0]) {
        return false;
    }
    for (int i = 0; i < observer->count; i++) {
        if (strcmp(observer->signatures[i], signature) == 0) {
            return true;
        }
    }
    if (observer->count < TOOL_FAILURE_SIGNATURE_LIMIT) {
        snprintf(observer->signatures[observer->count], sizeof(observer->signatures[observer->count]), "%s", signature);
        observer->count++;
    }
    return false;
}

static const char *priority_for_tool_failure(const char *tool_name, daima_err_t tool_err, const char *normalized)
{
    (void)tool_name;
    if (tool_err == DAIMA_ERR_NOT_FOUND) {
        return "P1";
    }
    if (normalized && (strcmp(normalized, "empty_input") == 0 ||
                       strcmp(normalized, "missing_path") == 0 ||
                       strcmp(normalized, "missing_content") == 0)) {
        return "P1";
    }
    return "P2";
}

static void add_string_array_item(cJSON *obj, const char *key, const char *value)
{
    cJSON *arr = cJSON_CreateArray();
    if (!arr) return;
    if (value && value[0]) {
        cJSON_AddItemToArray(arr, cJSON_CreateString(value));
    }
    cJSON_AddItemToObject(obj, key, arr);
}

static void collect_tool_failure_work_item(tool_failure_observer_t *observer,
                                           const daima_msg_t *msg,
                                           const char *tool_name,
                                           const char *tool_input,
                                           const char *tool_output,
                                           daima_err_t tool_err)
{
    if (!tool_name || tool_err == DAIMA_OK) {
        return;
    }

    const char *normalized = normalize_tool_failure_output(tool_name, tool_err, tool_input, tool_output);
    char signature[192];
    snprintf(signature, sizeof(signature), "tool:%s|err:%s|output:%s",
             tool_name, daima_err_to_name(tool_err), normalized ? normalized : "unknown");
    if (observer_seen_signature(observer, signature)) {
        return;
    }

    char input_preview[256];
    char output_preview[256];
    shorten_text(tool_input, input_preview, sizeof(input_preview), 220);
    shorten_text(tool_output, output_preview, sizeof(output_preview), 220);

    char title[256];
    if (tool_err == DAIMA_ERR_NOT_FOUND) {
        snprintf(title, sizeof(title), "模型调用未知工具 %s", tool_name);
    } else if (tool_input && strcmp(tool_input, "{}") == 0) {
        snprintf(title, sizeof(title), "工具 %s 收到空参数导致调用失败", tool_name);
    } else {
        snprintf(title, sizeof(title), "工具调用失败: %s", tool_name);
    }

    char desc[768];
    snprintf(desc, sizeof(desc), "工具 %s 执行失败，错误码 %s。input=%s output=%s",
             tool_name, daima_err_to_name(tool_err), input_preview, output_preview);

    cJSON *input = cJSON_CreateObject();
    if (!input) return;
    cJSON_AddStringToObject(input, "type", "defect");
    cJSON_AddStringToObject(input, "source", "log");
    cJSON_AddStringToObject(input, "title", title);
    cJSON_AddStringToObject(input, "description", desc);
    cJSON_AddStringToObject(input, "expected", "工具调用应使用存在的工具名，并提供 schema 要求的有效参数。");
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
                 tool_name, daima_err_to_name(tool_err), input_preview, output_preview);
        add_string_array_item(evidence, "logs", log_line);
        add_string_array_item(evidence, "files", "");
        add_string_array_item(evidence, "commands", "");

        cJSON *tool_calls = cJSON_CreateArray();
        cJSON *call = cJSON_CreateObject();
        if (tool_calls && call) {
            cJSON_AddStringToObject(call, "tool", tool_name);
            cJSON_AddStringToObject(call, "input", tool_input ? tool_input : "{}");
            cJSON_AddStringToObject(call, "error", daima_err_to_name(tool_err));
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
    daima_err_t err = work_item_store_collect_structured(input, &item);
    if (err == DAIMA_OK) {
        const char *id = cJSON_GetStringValue(cJSON_GetObjectItem(item, "id"));
        DAIMA_LOGI(TAG, "Collected work item for tool failure: %s (%s)", id ? id : "-", signature);
    } else {
        DAIMA_LOGW(TAG, "Collect work item for tool failure failed: %s", daima_err_to_name(err));
    }
    cJSON_Delete(item);
    cJSON_Delete(input);
}

cJSON *agent_turn_build_assistant_content(const llm_response_t *resp)
{
    cJSON *content = cJSON_CreateArray();

    if (resp->reasoning_content && resp->reasoning_content_len > 0) {
        cJSON *reasoning_block = cJSON_CreateObject();
        cJSON_AddStringToObject(reasoning_block, "type", "reasoning");
        cJSON_AddStringToObject(reasoning_block, "text", resp->reasoning_content);
        cJSON_AddItemToArray(content, reasoning_block);
    }

    if (resp->text && resp->text_len > 0) {
        cJSON *text_block = cJSON_CreateObject();
        cJSON_AddStringToObject(text_block, "type", "text");
        cJSON_AddStringToObject(text_block, "text", resp->text);
        cJSON_AddItemToArray(content, text_block);
    }

    for (int i = 0; i < resp->call_count; i++) {
        const llm_tool_call_t *call = &resp->calls[i];
        cJSON *tool_block = cJSON_CreateObject();
        cJSON_AddStringToObject(tool_block, "type", "tool_use");
        cJSON_AddStringToObject(tool_block, "id", call->id);
        cJSON_AddStringToObject(tool_block, "name", call->name);

        cJSON *input = cJSON_Parse(call->input);
        if (input) {
            cJSON_AddItemToObject(tool_block, "input", input);
        } else {
            cJSON_AddItemToObject(tool_block, "input", cJSON_CreateObject());
        }

        cJSON_AddItemToArray(content, tool_block);
    }

    return content;
}

char *agent_turn_generate_forced_final_response(const char *system_prompt,
                                                cJSON *messages,
                                                const char *reason)
{
    if (!system_prompt || !messages) {
        return NULL;
    }

    cJSON *user_msg = cJSON_CreateObject();
    if (!user_msg) {
        return NULL;
    }

    cJSON_AddStringToObject(user_msg, "role", "user");
    const char *prefix = reason && reason[0]
        ? reason
        : "工具调用轮次已达上限。";
    const char *suffix =
        "不要再调用任何工具。"
        "请仅基于当前已有的对话和工具结果，直接输出最终答复。"
        "如果任务还没完全完成，请明确说明已完成到哪一步、卡在什么地方，以及建议用户下一步怎么做。";
    size_t content_size = strlen(prefix) + strlen(suffix) + 2;
    char *content = calloc(1, content_size);
    if (!content) {
        cJSON_Delete(user_msg);
        return NULL;
    }
    snprintf(content, content_size, "%s%s", prefix, suffix);
    cJSON_AddStringToObject(user_msg, "content", content);
    free(content);
    cJSON_AddItemToArray(messages, user_msg);

    llm_response_t resp;
    memset(&resp, 0, sizeof(resp));
    daima_err_t err = llm_chat_tools(system_prompt, messages, NULL, &resp);
    if (err != DAIMA_OK || resp.tool_use || !resp.text || !resp.text[0]) {
        llm_response_free(&resp);
        return NULL;
    }

    char *final_text = strdup(resp.text);
    llm_response_free(&resp);
    return final_text;
}

static bool terminal_command_is_verification(const char *command)
{
    static const char *const keywords[] = {
        "cmake --build",
        "ctest",
        "make ",
        "ninja ",
        "pytest",
        "npm test",
        "pnpm test",
        "go test",
        "cargo test",
        "cargo check",
    };
    for (size_t i = 0; i < sizeof(keywords) / sizeof(keywords[0]); i++) {
        if (keywords[i] && command && strstr(command, keywords[i])) {
            return true;
        }
    }
    return false;
}

static bool should_auto_verify_code_path(const char *path)
{
    if (!path || !path[0]) {
        return false;
    }
    if (strcmp(path, "CMakeLists.txt") == 0 || daima_str_ends_with(path, "/CMakeLists.txt")) {
        return true;
    }
    return daima_str_ends_with(path, ".c") ||
           daima_str_ends_with(path, ".h") ||
           daima_str_ends_with(path, ".cc") ||
           daima_str_ends_with(path, ".cpp") ||
           daima_str_ends_with(path, ".cxx") ||
           daima_str_ends_with(path, ".hh") ||
           daima_str_ends_with(path, ".hpp") ||
           daima_str_ends_with(path, ".hxx");
}

static bool extract_tool_path(const char *tool_name, const char *tool_input, char *buf, size_t size)
{
    if (!tool_name || !tool_input || !buf || size == 0) {
        return false;
    }
    if (strcmp(tool_name, "apply_patch") != 0) {
        return false;
    }

    cJSON *root = cJSON_Parse(tool_input);
    if (!root) {
        return false;
    }
    const char *path = cJSON_GetStringValue(cJSON_GetObjectItem(root, "path"));
    if ((!path || !path[0]) && strcmp(tool_name, "apply_patch") == 0) {
        const char *patch = cJSON_GetStringValue(cJSON_GetObjectItem(root, "patch"));
        const char *marker = patch ? strstr(patch, "*** Update File: ") : NULL;
        if (!marker) marker = patch ? strstr(patch, "*** Add File: ") : NULL;
        if (!marker) marker = patch ? strstr(patch, "*** Delete File: ") : NULL;
        if (marker) {
            const char *start = strchr(marker, ':');
            if (start) {
                start++;
                while (*start == ' ') start++;
                size_t len = strcspn(start, "\r\n");
                if (len > 0 && len < size) {
                    memcpy(buf, start, len);
                    buf[len] = '\0';
                    cJSON_Delete(root);
                    return true;
                }
            }
        }
    }
    bool ok = false;
    if (path && path[0]) {
        snprintf(buf, size, "%s", path);
        ok = true;
    }
    cJSON_Delete(root);
    return ok;
}

static void record_turn_side_effects(turn_exec_stats_t *stats,
                                     const char *tool_name,
                                     const char *tool_input)
{
    if (!stats || !tool_name) {
        return;
    }

    if (strcmp(tool_name, "terminal") == 0) {
        cJSON *root = cJSON_Parse(tool_input ? tool_input : "{}");
        const char *command = NULL;
        if (root) {
            command = cJSON_GetStringValue(cJSON_GetObjectItem(root, "command"));
            if (!command || !command[0]) {
                command = cJSON_GetStringValue(cJSON_GetObjectItem(root, "cmd"));
            }
        }
        if (terminal_command_is_verification(command)) {
            stats->saw_explicit_verification = true;
        }
        cJSON_Delete(root);
        return;
    }

    char path[256];
    if (extract_tool_path(tool_name, tool_input, path, sizeof(path)) &&
        should_auto_verify_code_path(path)) {
        stats->modified_code_files = true;
        stats->saw_explicit_verification = false;
        snprintf(stats->last_modified_path, sizeof(stats->last_modified_path), "%s", path);
        char resolved_path[512];
        if (tool_files_resolve_write_path(path, resolved_path, sizeof(resolved_path))) {
            tool_files_get_recent_checkpoint(
                resolved_path,
                stats->last_checkpoint_path,
                sizeof(stats->last_checkpoint_path));
        }
    }
}

static bool pick_auto_verify_command(char *buf, size_t size)
{
    if (!buf || size == 0) {
        return false;
    }
    if (access("CMakeLists.txt", F_OK) == 0 && access("build-host", F_OK) == 0) {
        snprintf(buf, size, "cmake --build build-host -j4");
        return true;
    }
    if (access("CMakeLists.txt", F_OK) == 0 && access("build", F_OK) == 0) {
        snprintf(buf, size, "cmake --build build -j4");
        return true;
    }
    if (access("Makefile", F_OK) == 0) {
        snprintf(buf, size, "make -j4");
        return true;
    }
    return false;
}

static void extract_output_tail_snippet(const char *output, char *buf, size_t size)
{
    if (!buf || size == 0) {
        return;
    }
    buf[0] = '\0';
    if (!output || !output[0]) {
        return;
    }

    const char *start = output;
    const char *last_newline = strrchr(output, '\n');
    if (last_newline && last_newline[1]) {
        start = last_newline + 1;
    }

    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') {
        start++;
    }
    if (!start[0]) {
        start = output;
        while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') {
            start++;
        }
    }

    shorten_text(start, buf, size, 120);
}

static void append_verification_note(char **io_final_text,
                                     const char *command,
                                     const terminal_exec_result_t *result,
                                     const char *path_hint,
                                     const char *checkpoint_hint)
{
    if (!io_final_text || !*io_final_text || !command || !result) {
        return;
    }

    const char *target = (path_hint && path_hint[0]) ? path_hint : "本轮代码改动";
    char tail[160];
    char detail[512];
    extract_output_tail_snippet(result->output, tail, sizeof(tail));
    if (result->timed_out) {
        if (tail[0]) {
            snprintf(detail, sizeof(detail), "自动验证：已对 `%s` 执行 `%s`，结果：超时。最后输出：%s", target, command, tail);
        } else {
            snprintf(detail, sizeof(detail), "自动验证：已对 `%s` 执行 `%s`，结果：超时。", target, command);
        }
    } else if (result->exit_code == 0) {
        snprintf(detail, sizeof(detail), "自动验证：已对 `%s` 执行 `%s`，结果：通过。", target, command);
    } else {
        if (tail[0]) {
            snprintf(detail, sizeof(detail), "自动验证：已对 `%s` 执行 `%s`，结果：失败（exit %d）。最后输出：%s", target, command, result->exit_code, tail);
        } else {
            snprintf(detail, sizeof(detail), "自动验证：已对 `%s` 执行 `%s`，结果：失败（exit %d）。", target, command, result->exit_code);
        }
    }
    if (checkpoint_hint && checkpoint_hint[0] &&
        !result->timed_out && result->exit_code != 0 &&
        strlen(detail) + strlen(checkpoint_hint) + 48 < sizeof(detail)) {
        size_t len = strlen(detail);
        snprintf(detail + len, sizeof(detail) - len, " 可回滚检查点：%s", checkpoint_hint);
    }
    size_t old_len = strlen(*io_final_text);
    size_t note_len = strlen(detail);
    char *next = realloc(*io_final_text, old_len + note_len + 6);
    if (!next) {
        return;
    }
    *io_final_text = next;
    snprintf(*io_final_text + old_len, note_len + 6, "\n\n%s", detail);
}

void agent_turn_maybe_run_auto_verification(const turn_exec_stats_t *stats, char **io_final_text)
{
    if (!stats || !io_final_text || !*io_final_text) {
        return;
    }
    if (!stats->modified_code_files || stats->saw_explicit_verification) {
        return;
    }

    char command[128];
    if (!pick_auto_verify_command(command, sizeof(command))) {
        return;
    }

    terminal_exec_result_t result;
    memset(&result, 0, sizeof(result));
    daima_err_t err = terminal_execute_local_shell(command, NULL, 180, NULL, 4096, &result);
    if (err != DAIMA_OK) {
        free(result.output);
        return;
    }

    DAIMA_LOGI(TAG, "Auto verification: cmd=%s exit=%d timed_out=%d",
              command, result.exit_code, result.timed_out ? 1 : 0);

    if (result.exit_code != 0 || result.timed_out) {
        char title[256];
        snprintf(title, sizeof(title), "自动构建验证失败: %s", command);
        work_item_store_collect("defect", "test", title,
                                result.output ? result.output : "");
    }

    append_verification_note(io_final_text, command, &result, stats->last_modified_path, stats->last_checkpoint_path);
    free(result.output);
}

cJSON *agent_turn_build_tool_results(const llm_response_t *resp,
                                     const daima_msg_t *msg,
                                     char *tool_output,
                                     size_t tool_output_size,
                                     turn_exec_stats_t *stats)
{
    cJSON *content = cJSON_CreateArray();
    tool_failure_observer_t failure_observer = {0};

    for (int i = 0; i < resp->call_count; i++) {
        const llm_tool_call_t *call = &resp->calls[i];
        const char *tool_input = call->input ? call->input : "{}";
        daima_tool_runtime_result_t rt = {0};
        log_tool_payload_preview("before_runtime",
                                 msg,
                                 call->name,
                                 call->id,
                                 tool_input,
                                 NULL,
                                 DAIMA_OK);
        daima_err_t tool_err = tool_runtime_execute_call(call, msg, tool_output, tool_output_size, &rt);
        if (rt.effective_input) {
            tool_input = rt.effective_input;
        }
        log_tool_payload_preview(rt.effective_input ? "after_runtime_patched" : "after_runtime",
                                 msg,
                                 call->name,
                                 call->id,
                                 tool_input,
                                 tool_output,
                                 tool_err);
        record_turn_side_effects(stats, call->name, tool_input);
        agent_tool_feedback_send_activity(msg, call->name, tool_input, tool_output, tool_err, rt.elapsed_ms);
        collect_tool_failure_work_item(&failure_observer, msg, call->name, tool_input, tool_output, tool_err);
        if (agent_tool_protocol_failure_should_stop(call->name, tool_input, tool_output, tool_err)) {
            stats->unrecoverable_tool_protocol_error = true;
            snprintf(stats->tool_protocol_error_reason,
                     sizeof(stats->tool_protocol_error_reason),
                     "工具 %s 收到无效协议参数 input=%s",
                     call->name,
                     tool_input ? tool_input : "{}");
        }

        if (strcmp(call->name, "terminal") == 0 && tool_input) {
            if (terminal_command_is_verification(tool_input)) {
                cJSON *term_result = cJSON_Parse(tool_output);
                if (term_result) {
                    cJSON *ec = cJSON_GetObjectItem(term_result, "exit_code");
                    cJSON *to = cJSON_GetObjectItem(term_result, "timed_out");
                    bool failed = (ec && cJSON_IsNumber(ec) && ec->valueint != 0) ||
                                  (to && cJSON_IsBool(to) && cJSON_IsTrue(to));
                    cJSON_Delete(term_result);
                    if (failed) {
                        char title[256];
                        snprintf(title, sizeof(title), "验证命令失败: %.200s", tool_input);
                        work_item_store_collect("defect", "test", title, tool_output);
                    }
                }
            }
        } else if (strcmp(call->name, "webfetch") == 0 && tool_err != DAIMA_OK) {
            cJSON *wf_input = cJSON_Parse(tool_input);
            const char *wf_url = NULL;
            if (wf_input) {
                wf_url = cJSON_GetStringValue(cJSON_GetObjectItem(wf_input, "url"));
            }
            if (wf_url && wf_url[0]) {
                char title[256];
                snprintf(title, sizeof(title), "webfetch 失败: %.200s", wf_url);
                work_item_store_collect("defect", "test", title, tool_output);
            }
            cJSON_Delete(wf_input);
        }

        if (tool_err == DAIMA_OK) {
            DAIMA_LOGI(TAG, "Tool %s result: %d bytes", call->name, (int)strlen(tool_output));
        } else {
            char input_preview[240];
            char output_preview[240];
            shorten_text(tool_input, input_preview, sizeof(input_preview), 220);
            shorten_text(tool_output, output_preview, sizeof(output_preview), 220);
            DAIMA_LOGW(TAG, "Tool %s failed: %s input=%s output=%s",
                       call->name,
                       daima_err_to_name(tool_err),
                       input_preview,
                       output_preview);
        }

        cJSON *result_block = cJSON_CreateObject();
        cJSON_AddStringToObject(result_block, "type", "tool_result");
        cJSON_AddStringToObject(result_block, "tool_use_id", call->id);
        cJSON_AddStringToObject(result_block, "content", tool_output);
        cJSON_AddItemToArray(content, result_block);

        free(rt.effective_input);
    }

    return content;
}
