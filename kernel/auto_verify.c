/* 自动验证：代码改动后自动运行构建/测试 */
#include "auto_verify.h"
#include "linux/printk.h"
#include "text.h"
#include "drivers/tool/tool_file_ops.h"
#include "drivers/tool/tool_terminal_exec.h"
#include "work_item.h"
#include "linux/slab.h"
#include "linux/kernel.h"
#include "cjson.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool terminal_command_is_verification(const char *command)
{
    static const char *const keywords[] = {
        "cmake --build", "ctest", "make ", "ninja ",
        "pytest", "npm test", "pnpm test", "go test", "cargo test", "cargo check",
    };
    for (size_t i = 0; i < sizeof(keywords) / sizeof(keywords[0]); i++)
        if (keywords[i] && command && strstr(command, keywords[i])) return true;
    return false;
}

static bool should_auto_verify_code_path(const char *path)
{
    if (!path || !path[0]) return false;
    if (strcmp(path, "CMakeLists.txt") == 0 || str_ends_with(path, "/CMakeLists.txt")) return true;
    return str_ends_with(path, ".c") || str_ends_with(path, ".h") ||
           str_ends_with(path, ".cc") || str_ends_with(path, ".cpp") ||
           str_ends_with(path, ".cxx") || str_ends_with(path, ".hh") ||
           str_ends_with(path, ".hpp") || str_ends_with(path, ".hxx");
}

static bool extract_tool_path(const char *tool_name, const char *tool_input, char *buf, size_t size)
{
    if (!tool_name || !tool_input || !buf || size == 0) return false;
    if (strcmp(tool_name, "apply_patch") != 0) return false;

    cJSON *root = cJSON_Parse(tool_input);
    if (!root) return false;
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
                    memcpy(buf, start, len); buf[len] = '\0';
                    cJSON_Delete(root); return true;
                }
            }
        }
    }
    bool ok = false;
    if (path && path[0]) { strscpy(buf, path, size); ok = true; }
    cJSON_Delete(root);
    return ok;
}

void record_turn_side_effects(turn_exec_stats_t *stats,
                               const char *tool_name, const char *tool_input)
{
    if (!stats || !tool_name) return;

    if (strcmp(tool_name, "terminal") == 0) {
        cJSON *root = cJSON_Parse(tool_input ? tool_input : "{}");
        const char *command = NULL;
        if (root) {
            command = cJSON_GetStringValue(cJSON_GetObjectItem(root, "command"));
            if (!command || !command[0])
                command = cJSON_GetStringValue(cJSON_GetObjectItem(root, "cmd"));
        }
        if (terminal_command_is_verification(command))
            stats->saw_explicit_verification = true;
        cJSON_Delete(root);
        return;
    }

    char path[256];
    if (extract_tool_path(tool_name, tool_input, path, sizeof(path)) &&
        should_auto_verify_code_path(path)) {
        stats->modified_code_files = true;
        stats->saw_explicit_verification = false;
        strscpy(stats->last_modified_path, path, sizeof(stats->last_modified_path));
        char resolved_path[512];
        if (tool_files_resolve_write_path(path, resolved_path, sizeof(resolved_path)))
            tool_files_get_recent_checkpoint(resolved_path, stats->last_checkpoint_path,
                                             sizeof(stats->last_checkpoint_path));
    }
}

static bool pick_auto_verify_command(char *buf, size_t size)
{
    if (!buf || size == 0) return false;
    if (access("CMakeLists.txt", F_OK) == 0 && access("build-host", F_OK) == 0) {
        snprintf(buf, size, "cmake --build build-host -j4"); return true;
    }
    if (access("CMakeLists.txt", F_OK) == 0 && access("build", F_OK) == 0) {
        snprintf(buf, size, "cmake --build build -j4"); return true;
    }
    if (access("Makefile", F_OK) == 0) {
        snprintf(buf, size, "make -j4"); return true;
    }
    return false;
}

static void extract_output_tail(const char *output, char *buf, size_t size)
{
    if (!buf || size == 0) return;
    buf[0] = '\0';
    if (!output || !output[0]) return;
    const char *start = output;
    const char *ln = strrchr(output, '\n');
    if (ln && ln[1]) start = ln + 1;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') start++;
    text_shorten(start, buf, size, 120);
}

static void append_verification_note(char **io_final_text, const char *command,
                                      const terminal_exec_result_t *result,
                                      const char *path_hint, const char *checkpoint_hint)
{
    if (!io_final_text || !*io_final_text || !command || !result) return;
    const char *target = (path_hint && path_hint[0]) ? path_hint : "本轮代码改动";
    char tail[160], detail[512];
    extract_output_tail(result->output, tail, sizeof(tail));

    if (result->timed_out)
        snprintf(detail, sizeof(detail), "自动验证：已对 `%s` 执行 `%s`，结果：超时。%s%s",
                 target, command, tail[0] ? "最后输出：" : "", tail[0] ? tail : "");
    else if (result->exit_code == 0)
        snprintf(detail, sizeof(detail), "自动验证：已对 `%s` 执行 `%s`，结果：通过。", target, command);
    else
        snprintf(detail, sizeof(detail), "自动验证：已对 `%s` 执行 `%s`，结果：失败（exit %d）。%s%s",
                 target, command, result->exit_code, tail[0] ? "最后输出：" : "", tail[0] ? tail : "");

    if (checkpoint_hint && checkpoint_hint[0] && !result->timed_out && result->exit_code != 0) {
        size_t len = strlen(detail);
        snprintf(detail + len, sizeof(detail) - len, " 可回滚检查点：%s", checkpoint_hint);
    }
    size_t old_len = strlen(*io_final_text);
    size_t note_len = strlen(detail);
    char *next = realloc(*io_final_text, old_len + note_len + 6);
    if (!next) return;
    *io_final_text = next;
    snprintf(*io_final_text + old_len, note_len + 6, "\n\n%s", detail);
}

void agent_turn_maybe_run_auto_verification(const turn_exec_stats_t *stats, char **io_final_text)
{
    if (!stats || !io_final_text || !*io_final_text) return;
    if (!stats->modified_code_files || stats->saw_explicit_verification) return;

    char command[128];
    if (!pick_auto_verify_command(command, sizeof(command))) return;

    terminal_exec_result_t result;
    memset(&result, 0, sizeof(result));
    err_t err = terminal_execute_local_shell(command, NULL, 180, NULL, 4096, &result);
    if (err != 0) { kfree(result.output); return; }

    pr_info("Auto verification: cmd=%s exit=%d timed_out=%d",
            command, result.exit_code, result.timed_out ? 1 : 0);

    if (result.exit_code != 0 || result.timed_out) {
        char title[256];
        snprintf(title, sizeof(title), "自动构建验证失败: %s", command);
        work_item_store_collect("defect", "test", title, result.output ? result.output : "");
    }
    append_verification_note(io_final_text, command, &result,
                              stats->last_modified_path, stats->last_checkpoint_path);
    kfree(result.output);
}