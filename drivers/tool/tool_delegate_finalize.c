#include "drivers/tool/tool_delegate_finalize.h"

#include <string.h>

#include "drivers/tool/tool_delegate_overview.h"
#include "drivers/tool/tool_delegate_protocol.h"
#include "drivers/tool/tool_delegate_repo_batch.h"
#include "drivers/tool/tool_delegate_response.h"
#include "drivers/tool/tool_delegate_subagent.h"
#include "linux/kernel.h"
#include "text.h"

#define DELEGATE_RESULT_JSON_MAX 3072
#define DELEGATE_FINALIZER_RAW_CHAR_BUDGET 2200

static void append_clipped_text(char *dst, size_t dst_size, const char *src, size_t clip_chars)
{
    char clipped[768];

    if (!dst || dst_size == 0 || !src || !src[0]) {
        return;
    }
    text_shorten(src, clipped, sizeof(clipped), (int)clip_chars);
    strlcat(dst, clipped, dst_size);
}

static void collect_tool_result_corpus(cJSON *messages, char *out, size_t out_size)
{
    if (!messages || !cJSON_IsArray(messages) || !out || out_size == 0) {
        return;
    }

    out[0] = '\0';
    cJSON *msg = NULL;
    cJSON_ArrayForEach(msg, messages) {
        if (!cJSON_IsObject(msg)) {
            continue;
        }
        cJSON *content = cJSON_GetObjectItem(msg, "content");
        if (!content || !cJSON_IsArray(content)) {
            continue;
        }
        cJSON *block = NULL;
        cJSON_ArrayForEach(block, content) {
            const char *type = cJSON_GetStringValue(cJSON_GetObjectItem(block, "type"));
            const char *text = cJSON_GetStringValue(cJSON_GetObjectItem(block, "content"));
            if (!type || strcmp(type, "tool_result") != 0 || !text || !text[0]) {
                continue;
            }
            if (out[0]) {
                strlcat(out, "\n\n", out_size);
            }
            append_clipped_text(out, out_size, text, 1200);
            if (strlen(out) > DELEGATE_FINALIZER_RAW_CHAR_BUDGET) {
                return;
            }
        }
    }
}

static bool append_unique_line(char items[][96], int *count, int max_count, const char *line)
{
    if (!items || !count || !line || !line[0]) {
        return false;
    }
    for (int i = 0; i < *count; i++) {
        if (strcmp(items[i], line) == 0) {
            return false;
        }
    }
    if (*count >= max_count) {
        return false;
    }
    strscpy(items[*count], line, 96);
    (*count)++;
    return true;
}

static bool append_unique_path(char items[][160], int *count, int max_count, const char *line)
{
    if (!items || !count || !line || !line[0]) {
        return false;
    }
    for (int i = 0; i < *count; i++) {
        if (strcmp(items[i], line) == 0) {
            return false;
        }
    }
    if (*count >= max_count) {
        return false;
    }
    strscpy(items[*count], line, 160);
    (*count)++;
    return true;
}

static bool path_matches_any(const char *path, const char *const *paths, size_t count)
{
    if (!path || !path[0]) {
        return false;
    }
    for (size_t i = 0; i < count; i++) {
        if (!paths[i] || !paths[i][0]) {
            continue;
        }
        if (strcmp(path, paths[i]) == 0 || strstr(path, paths[i]) == path) {
            return true;
        }
    }
    return false;
}

static void append_joined_items(char *dst,
                                size_t dst_size,
                                char items[][96],
                                int count,
                                const char *separator)
{
    for (int i = 0; i < count; i++) {
        if (i > 0) {
            strlcat(dst, separator, dst_size);
        }
        strlcat(dst, items[i], dst_size);
    }
}

static void append_joined_paths(char *dst,
                                size_t dst_size,
                                char items[][160],
                                int count,
                                const char *separator)
{
    for (int i = 0; i < count; i++) {
        if (i > 0) {
            strlcat(dst, separator, dst_size);
        }
        strlcat(dst, items[i], dst_size);
    }
}

static bool delegate_try_render_repo_overview_from_tool_corpus(const delegate_request_t *req,
                                                               cJSON *messages,
                                                               char *summary,
                                                               size_t summary_size)
{
    char repo_root[512];
    char top_dirs[24][96];
    char kernel_dirs[24][96];
    char driver_dirs[24][96];
    char next_files[8][160];
    int top_count = 0;
    int kernel_count = 0;
    int driver_count = 0;
    int next_file_count = 0;
    bool saw_any = false;
    static const char *const preferred_top_dirs[] = {
        "init", "kernel", "drivers", "include", "arch", "docs", "scripts",
        "spiffs_data", "ipc", "lib", "net", "fs"
    };
    static const char *const preferred_next_files[] = {
        "init/main.c",
        "init/bootstrap.c",
        "kernel/loop.c",
        "kernel/turn/turn_run.c",
        "kernel/turn/turn_exec.c",
        "kernel/context/context_build.c",
        "drivers/tool/tool_delegate.c",
        "drivers/tool/tool_invocation_context.c"
    };

    if (!req || !messages || !cJSON_IsArray(messages) || !summary || summary_size == 0) {
        return false;
    }
    if (!tool_delegate_request_is_bounded_explore_overview(req)) {
        return false;
    }
    if (!tool_delegate_resolve_repo_root(req, repo_root, sizeof(repo_root))) {
        return false;
    }

    cJSON *msg = NULL;
    cJSON_ArrayForEach(msg, messages) {
        if (!cJSON_IsObject(msg)) {
            continue;
        }
        cJSON *content = cJSON_GetObjectItem(msg, "content");
        if (!content || !cJSON_IsArray(content)) {
            continue;
        }
        cJSON *block = NULL;
        cJSON_ArrayForEach(block, content) {
            const char *type = cJSON_GetStringValue(cJSON_GetObjectItem(block, "type"));
            const char *text = cJSON_GetStringValue(cJSON_GetObjectItem(block, "content"));
            if (!type || strcmp(type, "tool_result") != 0 || !text || !text[0]) {
                continue;
            }

            const char *cursor = text;
            while (cursor && *cursor) {
                const char *line_end = strchr(cursor, '\n');
                size_t len = line_end ? (size_t)(line_end - cursor) : strlen(cursor);
                if (len > 0 && len < 1024 && strncmp(cursor, repo_root, strlen(repo_root)) == 0) {
                    char line[1024];
                    char rel[512];
                    const char *suffix = NULL;

                    memcpy(line, cursor, len);
                    line[len] = '\0';
                    suffix = line + strlen(repo_root);
                    while (*suffix == '/') {
                        suffix++;
                    }
                    strscpy(rel, suffix, sizeof(rel));
                    if (rel[0]) {
                        char rel_head[512];
                        strscpy(rel_head, rel, sizeof(rel_head));
                        char *slash = strchr(rel_head, '/');
                        if (slash) {
                            *slash = '\0';
                        }
                        if (path_matches_any(rel_head,
                                             preferred_top_dirs,
                                             ARRAY_SIZE(preferred_top_dirs)) &&
                            append_unique_line(top_dirs, &top_count, 24, rel_head)) {
                            saw_any = true;
                        }
                        if (path_matches_any(rel,
                                             preferred_next_files,
                                             ARRAY_SIZE(preferred_next_files))) {
                            append_unique_path(next_files, &next_file_count, 8, rel);
                        }
                        if (strncmp(suffix, "kernel/", 7) == 0) {
                            const char *sub = suffix + 7;
                            char name[96];
                            const char *sub_end = strchr(sub, '/');
                            size_t sub_len = sub_end ? (size_t)(sub_end - sub) : strlen(sub);
                            bool treat_as_dir = sub_end != NULL || strchr(sub, '.') == NULL;
                            if (treat_as_dir && sub_len > 0 && sub_len < sizeof(name)) {
                                memcpy(name, sub, sub_len);
                                name[sub_len] = '\0';
                                append_unique_line(kernel_dirs, &kernel_count, 24, name);
                            }
                        }
                        if (strncmp(suffix, "drivers/", 8) == 0) {
                            const char *sub = suffix + 8;
                            char name[96];
                            const char *sub_end = strchr(sub, '/');
                            size_t sub_len = sub_end ? (size_t)(sub_end - sub) : strlen(sub);
                            bool treat_as_dir = sub_end != NULL || strchr(sub, '.') == NULL;
                            if (treat_as_dir && sub_len > 0 && sub_len < sizeof(name)) {
                                memcpy(name, sub, sub_len);
                                name[sub_len] = '\0';
                                append_unique_line(driver_dirs, &driver_count, 24, name);
                            }
                        }
                    }
                }
                cursor = line_end ? line_end + 1 : NULL;
            }
        }
    }

    if (!saw_any || top_count == 0) {
        return false;
    }

    summary[0] = '\0';
    strlcat(summary, "目录结构已经足够清楚：这个仓库主要分成启动入口、执行内核、驱动适配层、公共头文件和外围支撑目录几块。", summary_size);
    if (top_count > 0) {
        strlcat(summary, "\n\n关键目录：", summary_size);
        append_joined_items(summary, summary_size, top_dirs, top_count, "、");
    }
    if (kernel_count > 0) {
        strlcat(summary, "\n- `kernel/` 下已识别到的主链子目录：", summary_size);
        append_joined_items(summary, summary_size, kernel_dirs, kernel_count, "、");
    }
    if (driver_count > 0) {
        strlcat(summary, "\n- `drivers/` 下已识别到的适配层目录：", summary_size);
        append_joined_items(summary, summary_size, driver_dirs, driver_count, "、");
    }
    strlcat(summary,
            "\n\n模块判断：`init/` 负责进程启动和 bootstrap；`kernel/turn`、`kernel/context`、`kernel/channel`、`kernel/tooling` 组成主执行链路；`drivers/tool`、`drivers/llm`、`drivers/channel` 承担工具、模型和通信适配层职责。",
            summary_size);
    strlcat(summary,
            "\n- 结合已读取的 `init/main.c` 和 `kernel/loop.c`，可以确认启动入口和主循环已经拆开：前者负责启动流程，后者负责消息驱动与恢复执行。",
            summary_size);
    if (next_file_count > 0) {
        strlcat(summary, "\n\n建议继续看：", summary_size);
        append_joined_paths(summary, summary_size, next_files, next_file_count, "、");
    }
    strlcat(summary,
            "\n\n结论性质：这份总结基于目录枚举和少量核心文件抽样，已经足够支持父代理继续拆分子任务，但还不是完整静态审计。",
            summary_size);
    return true;
}

err_t tool_delegate_finalize_sync_response(delegate_subagent_kind_t kind,
                                           const delegate_request_t *req,
                                           const char *session_id,
                                           cJSON *messages,
                                           const char *final_text,
                                           const char *reasoning_text,
                                           const char *raw_final_text,
                                           const char *raw_reasoning_text,
                                           bool tool_budget_exhausted,
                                           bool cancelled,
                                           char *output,
                                           size_t output_size)
{
    char tool_result_corpus[DELEGATE_RESULT_JSON_MAX];
    char final_json_summary[DELEGATE_RESULT_JSON_MAX];
    char reasoning_json_summary[DELEGATE_RESULT_JSON_MAX];
    const char *subagent_type = req && req->subagent_type[0] ? req->subagent_type : "";
    const char *description = req && req->description[0] ? req->description : "";
    const char *model = tool_delegate_subagent_model_for_kind(kind);
    const char *finalizer_source = NULL;

    if (!req || !output || output_size == 0) {
        return ERR_INVALID_ARG;
    }

    collect_tool_result_corpus(messages, tool_result_corpus, sizeof(tool_result_corpus));

    if (tool_delegate_parse_result_json_rendered(final_text, final_json_summary, sizeof(final_json_summary))) {
        return tool_delegate_write_json_response(output, output_size, NULL, session_id, "done", "sync_final",
                                                 subagent_type, description, model, final_json_summary);
    }
    if (tool_delegate_parse_result_json_rendered(reasoning_text, reasoning_json_summary, sizeof(reasoning_json_summary))) {
        return tool_delegate_write_json_response(output, output_size, NULL, session_id, "done", "sync_final",
                                                 subagent_type, description, model, reasoning_json_summary);
    }
    if (delegate_try_render_repo_overview_from_tool_corpus(req,
                                                           messages,
                                                           final_json_summary,
                                                           sizeof(final_json_summary))) {
        return tool_delegate_write_json_response(output, output_size, NULL, session_id, "done", "sync_final",
                                                 subagent_type, description, model, final_json_summary);
    }
    if (tool_result_corpus[0] &&
        (tool_delegate_try_fast_local_json(subagent_type,
                                           description,
                                           tool_result_corpus,
                                           final_json_summary,
                                           sizeof(final_json_summary)) ||
         tool_delegate_finalize_result_json(subagent_type,
                                            description,
                                            tool_result_corpus,
                                            final_json_summary,
                                            sizeof(final_json_summary)))) {
        return tool_delegate_write_json_response(output, output_size, NULL, session_id, "done", "sync_final",
                                                 subagent_type, description, model, final_json_summary);
    }

    finalizer_source = (raw_final_text && raw_final_text[0]) ? raw_final_text :
                       ((raw_reasoning_text && raw_reasoning_text[0]) ? raw_reasoning_text : NULL);
    if (finalizer_source &&
        (tool_delegate_try_fast_local_json(subagent_type,
                                           description,
                                           finalizer_source,
                                           final_json_summary,
                                           sizeof(final_json_summary)) ||
         tool_delegate_finalize_result_json(subagent_type,
                                            description,
                                            finalizer_source,
                                            final_json_summary,
                                            sizeof(final_json_summary)))) {
        return tool_delegate_write_json_response(output, output_size, NULL, session_id, "done", "sync_final",
                                                 subagent_type, description, model, final_json_summary);
    }

    {
        char safe[1024];
        tool_delegate_build_safe_output_text(raw_final_text ? raw_final_text : final_text,
                                             raw_reasoning_text ? raw_reasoning_text : reasoning_text,
                                             tool_budget_exhausted,
                                             cancelled,
                                             safe,
                                             sizeof(safe));
        return tool_delegate_write_json_response(output, output_size, NULL, session_id,
                                                 tool_delegate_safe_text_is_directly_usable(safe) ? "done" : "blocked",
                                                 "sync_final",
                                                 subagent_type,
                                                 description,
                                                 model,
                                                 safe);
    }
}
