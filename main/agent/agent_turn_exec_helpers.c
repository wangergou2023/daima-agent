#include "agent/agent_turn_exec_helpers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "app/channel_runtime.h"
#include "daima_log.h"
#include "daima_text.h"
#include "tools/tool_file_ops.h"
#include "tools/tool_runtime.h"
#include "tools/tool_terminal_exec.h"

static const char *TAG = "agent_run";

static const char *tool_display_icon(const char *tool_name)
{
    if (!tool_name) return "⚙";
    if (strcmp(tool_name, "terminal") == 0) return "💻";
    if (strcmp(tool_name, "read_file") == 0) return "📖";
    if (strcmp(tool_name, "write_file") == 0 || strcmp(tool_name, "edit_file") == 0) return "✏️";
    if (strcmp(tool_name, "list_dir") == 0) return "🗂️";
    if (strcmp(tool_name, "weather") == 0) return "🌤️";
    if (strcmp(tool_name, "get_current_time") == 0) return "🕒";
    if (strcmp(tool_name, "cron_add") == 0 || strcmp(tool_name, "cron_list") == 0 || strcmp(tool_name, "cron_remove") == 0) return "⏰";
    return "⚙";
}

static const char *tool_display_name(const char *tool_name)
{
    if (!tool_name) return "tool";
    return tool_name;
}

static const char *path_tail(const char *path)
{
    if (!path || !path[0]) return "";
    const char *slash = strrchr(path, '/');
    return (slash && slash[1]) ? slash + 1 : path;
}

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

char *agent_turn_generate_forced_final_response(const char *system_prompt, cJSON *messages)
{
    if (!system_prompt || !messages) {
        return NULL;
    }

    cJSON *user_msg = cJSON_CreateObject();
    if (!user_msg) {
        return NULL;
    }

    cJSON_AddStringToObject(user_msg, "role", "user");
    cJSON_AddStringToObject(
        user_msg,
        "content",
        "工具调用轮次已达上限。不要再调用任何工具。"
        "请仅基于当前已有的对话和工具结果，直接输出最终答复。"
        "如果任务还没完全完成，请明确说明已完成到哪一步、卡在什么地方，以及建议用户下一步怎么做。");
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
    if (strcmp(tool_name, "write_file") != 0 &&
        strcmp(tool_name, "edit_file") != 0 &&
        strcmp(tool_name, "patch") != 0) {
        return false;
    }

    cJSON *root = cJSON_Parse(tool_input);
    if (!root) {
        return false;
    }
    const char *path = cJSON_GetStringValue(cJSON_GetObjectItem(root, "path"));
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
    append_verification_note(io_final_text, command, &result, stats->last_modified_path, stats->last_checkpoint_path);
    free(result.output);
}

static void summarize_tool_target(const char *tool_name, const char *tool_input, char *buf, size_t size)
{
    if (!buf || size == 0) return;
    buf[0] = '\0';

    cJSON *root = cJSON_Parse(tool_input ? tool_input : "{}");
    const char *value = NULL;

    if (root && cJSON_IsObject(root)) {
        if (tool_name && strcmp(tool_name, "terminal") == 0) {
            value = cJSON_GetStringValue(cJSON_GetObjectItem(root, "command"));
            if (!value || !value[0]) {
                value = cJSON_GetStringValue(cJSON_GetObjectItem(root, "cmd"));
            }
        } else if (strcmp(tool_name, "read_file") == 0 ||
                   strcmp(tool_name, "write_file") == 0 ||
                   strcmp(tool_name, "edit_file") == 0) {
            value = path_tail(cJSON_GetStringValue(cJSON_GetObjectItem(root, "path")));
        } else if (strcmp(tool_name, "list_dir") == 0) {
            value = path_tail(cJSON_GetStringValue(cJSON_GetObjectItem(root, "path")));
            if (!value || !value[0]) {
                value = cJSON_GetStringValue(cJSON_GetObjectItem(root, "prefix"));
            }
        } else if (strcmp(tool_name, "weather") == 0) {
            value = cJSON_GetStringValue(cJSON_GetObjectItem(root, "location"));
        } else if (strcmp(tool_name, "cron_add") == 0) {
            value = cJSON_GetStringValue(cJSON_GetObjectItem(root, "name"));
        } else if (strcmp(tool_name, "cron_remove") == 0) {
            value = cJSON_GetStringValue(cJSON_GetObjectItem(root, "job_id"));
        } else if (strcmp(tool_name, "search_files") == 0) {
            value = cJSON_GetStringValue(cJSON_GetObjectItem(root, "pattern"));
            if (!value || !value[0]) {
                value = cJSON_GetStringValue(cJSON_GetObjectItem(root, "query"));
            }
        } else if (strcmp(tool_name, "session_search") == 0) {
            value = cJSON_GetStringValue(cJSON_GetObjectItem(root, "query"));
        } else if (strcmp(tool_name, "skill_view") == 0) {
            value = cJSON_GetStringValue(cJSON_GetObjectItem(root, "name"));
        }
    }

    if (value && value[0]) {
        shorten_text(value, buf, size, 44);
    }
    cJSON_Delete(root);
}

static bool tool_result_success(const char *tool_name, daima_err_t exec_err, const char *tool_output, char *detail, size_t detail_size)
{
    if (detail && detail_size > 0) {
        detail[0] = '\0';
    }

    if (tool_name && strcmp(tool_name, "terminal") == 0) {
        cJSON *root = cJSON_Parse(tool_output ? tool_output : "{}");
        if (!root) {
            if (detail && detail_size > 0) snprintf(detail, detail_size, "执行失败");
            return false;
        }
        int exit_code = -1;
        cJSON *exit_j = cJSON_GetObjectItem(root, "exit_code");
        if (exit_j && cJSON_IsNumber(exit_j)) {
            exit_code = (int)exit_j->valuedouble;
        }
        cJSON *status_j = cJSON_GetObjectItem(root, "status");
        const char *status = cJSON_IsString(status_j) ? status_j->valuestring : NULL;
        cJSON *timed_j = cJSON_GetObjectItem(root, "timed_out");
        bool timed_out = cJSON_IsTrue(timed_j);
        bool ok = (exit_code == 0);

        if (detail && detail_size > 0) {
            if (timed_out) {
                snprintf(detail, detail_size, "超时");
            } else if (status && status[0] && strcmp(status, "ok") != 0) {
                snprintf(detail, detail_size, "%s", status);
            } else if (exit_code >= 0) {
                snprintf(detail, detail_size, "exit %d", exit_code);
            }
        }
        cJSON_Delete(root);
        return ok;
    }

    if (exec_err == DAIMA_OK) {
        return true;
    }

    if (detail && detail_size > 0) {
        snprintf(detail, detail_size, "%s", daima_err_to_name(exec_err));
    }
    return false;
}

static void maybe_send_tool_activity(const daima_msg_t *msg,
                                     const char *tool_name,
                                     const char *tool_input,
                                     const char *tool_output,
                                     daima_err_t exec_err,
                                     long elapsed_ms)
{
    if (!msg) {
        return;
    }

    char target[96];
    char detail[64];
    char line[256];
    summarize_tool_target(tool_name, tool_input, target, sizeof(target));

    bool ok = tool_result_success(tool_name, exec_err, tool_output, detail, sizeof(detail));
    if (target[0]) {
        if (ok) {
            snprintf(line, sizeof(line), "%s %s · %s · %.1fs",
                     tool_display_icon(tool_name),
                     tool_display_name(tool_name),
                     target,
                     (double)elapsed_ms / 1000.0);
        } else {
            snprintf(line, sizeof(line), "%s %s · %s · 失败%s%s",
                     tool_display_icon(tool_name),
                     tool_display_name(tool_name),
                     target,
                     detail[0] ? "：" : "",
                     detail);
        }
    } else {
        if (ok) {
            snprintf(line, sizeof(line), "%s %s · %.1fs",
                     tool_display_icon(tool_name),
                     tool_display_name(tool_name),
                     (double)elapsed_ms / 1000.0);
        } else {
            snprintf(line, sizeof(line), "%s %s · 失败%s%s",
                     tool_display_icon(tool_name),
                     tool_display_name(tool_name),
                     detail[0] ? "：" : "",
                     detail);
        }
    }

    daima_tool_activity_event_t event = {
        .tool_name = tool_name,
        .tool_input = tool_input,
        .target = target,
        .detail = detail,
        .default_text = line,
        .ok = ok,
        .elapsed_ms = elapsed_ms,
    };
    daima_err_t send_err = channel_runtime_send_tool_activity(msg, &event);
    if (send_err != DAIMA_OK) {
        DAIMA_LOGW(TAG, "Tool activity send failed for %s:%s: %s",
                  msg->channel, msg->chat_id, daima_err_to_name(send_err));
    }
}

cJSON *agent_turn_build_tool_results(const llm_response_t *resp,
                                     const daima_msg_t *msg,
                                     char *tool_output,
                                     size_t tool_output_size,
                                     turn_exec_stats_t *stats)
{
    cJSON *content = cJSON_CreateArray();

    for (int i = 0; i < resp->call_count; i++) {
        const llm_tool_call_t *call = &resp->calls[i];
        const char *tool_input = call->input ? call->input : "{}";
        daima_tool_runtime_result_t rt = {0};
        daima_err_t tool_err = tool_runtime_execute_call(call, msg, tool_output, tool_output_size, &rt);
        if (rt.effective_input) {
            tool_input = rt.effective_input;
        }
        record_turn_side_effects(stats, call->name, tool_input);
        maybe_send_tool_activity(msg, call->name, tool_input, tool_output, tool_err, rt.elapsed_ms);

        DAIMA_LOGI(TAG, "Tool %s result: %d bytes", call->name, (int)strlen(tool_output));

        cJSON *result_block = cJSON_CreateObject();
        cJSON_AddStringToObject(result_block, "type", "tool_result");
        cJSON_AddStringToObject(result_block, "tool_use_id", call->id);
        cJSON_AddStringToObject(result_block, "content", tool_output);
        cJSON_AddItemToArray(content, result_block);

        free(rt.effective_input);
    }

    return content;
}
