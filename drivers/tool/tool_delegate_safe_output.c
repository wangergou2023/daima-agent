#include "drivers/tool/tool_delegate_safe_output.h"

#include <string.h>

#include "cjson.h"
#include "drivers/tool/tool_delegate_result_json.h"
#include "drivers/tool/tool_delegate_sanitize.h"
#include "linux/kernel.h"
#include "text.h"

#define DELEGATE_RESULT_JSON_MAX 3072
#define DELEGATE_FALLBACK_EXCERPT_CHARS 480

static void append_clipped_text(char *dst, size_t dst_size, const char *src, size_t clip_chars)
{
    if (!dst || dst_size == 0 || !src || !src[0]) {
        return;
    }

    size_t dst_len = strlen(dst);
    if (dst_len >= dst_size - 1) {
        return;
    }

    size_t src_len = strlen(src);
    bool clipped = src_len > clip_chars;
    size_t copy_len = clipped ? clip_chars : src_len;
    size_t remain = dst_size - 1 - dst_len;
    if (copy_len > remain) {
        copy_len = remain;
        clipped = true;
    }

    if (copy_len > 0) {
        memcpy(dst + dst_len, src, copy_len);
        dst[dst_len + copy_len] = '\0';
    }

    if (clipped && strlen(dst) + 32 < dst_size) {
        strlcat(dst, "\n...[truncated by delegate_task]", dst_size);
    }
}

static bool tool_delegate_text_is_tool_protocol_json(const char *text)
{
    cJSON *root;
    bool is_tool_protocol = false;

    if (!text || !text[0]) {
        return false;
    }

    root = cJSON_Parse(text);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }

    if (cJSON_GetObjectItem(root, "action") && cJSON_GetObjectItem(root, "path")) {
        is_tool_protocol = true;
    } else if (cJSON_GetObjectItem(root, "tool_type") || cJSON_GetObjectItem(root, "tool_input")) {
        is_tool_protocol = true;
    } else if (cJSON_GetObjectItem(root, "tool_use_id")) {
        is_tool_protocol = true;
    } else {
        const char *status = cJSON_GetStringValue(cJSON_GetObjectItem(root, "status"));
        if (status &&
            (!strcmp(status, "merged_into_batch") ||
             !strcmp(status, "merged_into_sync_delegate"))) {
            is_tool_protocol = true;
        }
    }

    cJSON_Delete(root);
    return is_tool_protocol;
}

static bool tool_delegate_render_terminal_result_summary(const char *text,
                                                         char *summary,
                                                         size_t summary_size)
{
    cJSON *root;
    const char *command;
    const char *workdir;
    const char *output;
    const char *error;
    cJSON *exit_code_item;
    int exit_code = 0;
    bool has_exit_code = false;
    bool timed_out = false;
    bool truncated = false;
    char output_preview[256];

    if (!text || !text[0] || !summary || summary_size == 0) {
        return false;
    }

    root = cJSON_Parse(text);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }

    command = cJSON_GetStringValue(cJSON_GetObjectItem(root, "command"));
    workdir = cJSON_GetStringValue(cJSON_GetObjectItem(root, "workdir"));
    output = cJSON_GetStringValue(cJSON_GetObjectItem(root, "output"));
    error = cJSON_GetStringValue(cJSON_GetObjectItem(root, "error"));
    exit_code_item = cJSON_GetObjectItem(root, "exit_code");
    if (exit_code_item && cJSON_IsNumber(exit_code_item)) {
        exit_code = exit_code_item->valueint;
        has_exit_code = true;
    }
    if (cJSON_IsBool(cJSON_GetObjectItem(root, "timed_out"))) {
        timed_out = cJSON_IsTrue(cJSON_GetObjectItem(root, "timed_out"));
    }
    if (cJSON_IsBool(cJSON_GetObjectItem(root, "truncated"))) {
        truncated = cJSON_IsTrue(cJSON_GetObjectItem(root, "truncated"));
    }

    if (!command || !command[0] || !has_exit_code) {
        cJSON_Delete(root);
        return false;
    }

    summary[0] = '\0';
    if (exit_code == 0) {
        snprintf(summary, summary_size, "命令 `%s` 执行成功", command);
    } else if (error && strcmp(error, "sudo_password_cancelled") == 0) {
        snprintf(summary,
                 summary_size,
                 "命令 `%s` 未完成：它需要 sudo 权限，但这次没有提供 sudo password，所以被取消了（exit_code=%d）。像 `/root` 这样的路径通常只有 root 可读。",
                 command,
                 exit_code);
    } else if (timed_out) {
        snprintf(summary, summary_size, "命令 `%s` 执行超时（exit_code=%d）。", command, exit_code);
    } else if (error && error[0]) {
        snprintf(summary, summary_size, "命令 `%s` 执行失败：%s（exit_code=%d）。", command, error, exit_code);
    } else {
        snprintf(summary, summary_size, "命令 `%s` 执行结束，exit_code=%d。", command, exit_code);
    }

    if (workdir && workdir[0] && strlen(summary) + strlen(workdir) + 20 < summary_size) {
        strlcat(summary, "\n工作目录：", summary_size);
        strlcat(summary, workdir, summary_size);
    }

    output_preview[0] = '\0';
    if (output && output[0]) {
        text_shorten(output, output_preview, sizeof(output_preview), 180);
        if (output_preview[0] && strlen(summary) + strlen(output_preview) + 16 < summary_size) {
            strlcat(summary, "\n输出摘要：", summary_size);
            strlcat(summary, output_preview, summary_size);
            if (truncated && strlen(summary) + 16 < summary_size) {
                strlcat(summary, " [truncated]", summary_size);
            }
        }
    }

    cJSON_Delete(root);
    return true;
}

static void build_delegate_non_json_failure(const char *final_text,
                                            const char *reasoning_text,
                                            bool tool_budget_exhausted,
                                            bool cancelled,
                                            char *summary,
                                            size_t summary_size)
{
    char final_clean[DELEGATE_RESULT_JSON_MAX];
    char reasoning_clean[DELEGATE_RESULT_JSON_MAX];

    if (!summary || summary_size == 0) {
        return;
    }

    summary[0] = '\0';
    final_clean[0] = '\0';
    reasoning_clean[0] = '\0';
    tool_delegate_sanitize_summary_text_copy(final_clean, sizeof(final_clean), final_text);
    tool_delegate_sanitize_summary_text_copy(reasoning_clean, sizeof(reasoning_clean), reasoning_text);

    if (cancelled) {
        strscpy(summary, "delegate_task: subagent cancelled", summary_size);
        return;
    }
    if (tool_budget_exhausted &&
        reasoning_clean[0] &&
        !tool_delegate_text_has_dsml_markup(reasoning_clean) &&
        !tool_delegate_text_has_transcript_markup_public(reasoning_clean)) {
        strscpy(summary, reasoning_clean, summary_size);
        return;
    }
    if (tool_budget_exhausted &&
        final_clean[0] &&
        !tool_delegate_text_has_dsml_markup(final_clean) &&
        !tool_delegate_text_has_transcript_markup_public(final_clean)) {
        strscpy(summary, final_clean, summary_size);
        return;
    }
    if (tool_budget_exhausted) {
        char final_preview[256];
        char reasoning_preview[256];
        text_shorten(final_clean, final_preview, sizeof(final_preview), 220);
        text_shorten(reasoning_clean, reasoning_preview, sizeof(reasoning_preview), 220);
        pr_info("delegate_non_json_failure budget fallback: final_clean=%s reasoning_clean=%s final_has_transcript=%d reasoning_has_transcript=%d",
                final_preview,
                reasoning_preview,
                tool_delegate_text_has_transcript_markup_public(final_clean) ? 1 : 0,
                tool_delegate_text_has_transcript_markup_public(reasoning_clean) ? 1 : 0);
    }
    if (tool_budget_exhausted) {
        strscpy(summary,
                "delegate_task: tool iteration budget exhausted before producing a valid JSON result",
                summary_size);
        return;
    }
    if ((final_text && final_text[0] &&
         (tool_delegate_text_has_dsml_markup(final_text) ||
          tool_delegate_text_has_transcript_markup_public(final_text))) ||
        (reasoning_text && reasoning_text[0] &&
         (tool_delegate_text_has_dsml_markup(reasoning_text) ||
          tool_delegate_text_has_transcript_markup_public(reasoning_text)))) {
        strscpy(summary,
                "delegate_task: subagent returned tool markup/transcript instead of protocol JSON",
                summary_size);
        return;
    }

    strscpy(summary, "delegate_task: subagent returned non-JSON result after finalizer failed", summary_size);
    if ((final_text && final_text[0]) || (reasoning_text && reasoning_text[0])) {
        strlcat(summary, "\n\nExcerpt:\n", summary_size);
        append_clipped_text(summary,
                            summary_size,
                            (final_text && final_text[0]) ? final_text : reasoning_text,
                            DELEGATE_FALLBACK_EXCERPT_CHARS);
    }
}

void tool_delegate_build_safe_output_text(const char *final_text,
                                          const char *reasoning_text,
                                          bool tool_budget_exhausted,
                                          bool cancelled,
                                          char *summary,
                                          size_t summary_size)
{
    if (!summary || summary_size == 0) {
        return;
    }
    summary[0] = '\0';
    if (tool_delegate_parse_result_json_summary(final_text, summary, summary_size) ||
        tool_delegate_parse_result_json_summary(reasoning_text, summary, summary_size) ||
        tool_delegate_render_terminal_result_summary(final_text, summary, summary_size) ||
        tool_delegate_render_terminal_result_summary(reasoning_text, summary, summary_size)) {
        return;
    }
    build_delegate_non_json_failure(final_text,
                                    reasoning_text,
                                    tool_budget_exhausted,
                                    cancelled,
                                    summary,
                                    summary_size);
}

bool tool_delegate_try_fast_local_json(const char *subagent_type,
                                       const char *description,
                                       const char *raw_text,
                                       char *summary,
                                       size_t summary_size)
{
    (void)subagent_type;
    (void)description;

    if (!raw_text || !raw_text[0] || !summary || summary_size == 0) {
        return false;
    }

    char clean[DELEGATE_RESULT_JSON_MAX];
    char safe[1024];

    tool_delegate_sanitize_summary_text_copy(clean, sizeof(clean), raw_text);

    if (tool_delegate_parse_result_json_summary(clean, summary, summary_size)) {
        return true;
    }

    if (tool_delegate_render_terminal_result_summary(clean, summary, summary_size)) {
        return true;
    }

    if (tool_delegate_text_is_tool_protocol_json(clean)) {
        return false;
    }

    if (tool_delegate_text_has_dsml_markup(clean) || tool_delegate_text_has_transcript_markup_public(clean)) {
        return false;
    }

    tool_delegate_build_safe_output_text(clean, "", true, false, safe, sizeof(safe));
    if (!safe[0] || !tool_delegate_safe_text_is_directly_usable(safe)) {
        return false;
    }

    strscpy(summary, safe, summary_size);
    return true;
}
