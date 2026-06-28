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
