#include "drivers/tool/tool_delegate_result_json.h"

#include <stdio.h>
#include <string.h>

#include "cjson.h"
#include "linux/kernel.h"
#include "text.h"

static bool json_bool_value(cJSON *item, bool *value)
{
    if (!item || !value) {
        return false;
    }
    if (cJSON_IsBool(item)) {
        *value = cJSON_IsTrue(item);
        return true;
    }
    return false;
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
    json_bool_value(cJSON_GetObjectItem(root, "timed_out"), &timed_out);
    json_bool_value(cJSON_GetObjectItem(root, "truncated"), &truncated);

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
        snprintf(summary,
                 summary_size,
                 "命令 `%s` 执行超时（exit_code=%d）。",
                 command,
                 exit_code);
    } else if (error && error[0]) {
        snprintf(summary,
                 summary_size,
                 "命令 `%s` 执行失败：%s（exit_code=%d）。",
                 command,
                 error,
                 exit_code);
    } else {
        snprintf(summary,
                 summary_size,
                 "命令 `%s` 执行结束，exit_code=%d。",
                 command,
                 exit_code);
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

static void append_string_array_section(char *summary,
                                        size_t summary_size,
                                        cJSON *array,
                                        const char *title)
{
    if (!summary || summary_size == 0 || !array || !cJSON_IsArray(array) ||
        cJSON_GetArraySize(array) <= 0 || !title) {
        return;
    }

    strlcat(summary, title, summary_size);
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, array) {
        const char *value = cJSON_GetStringValue(item);
        if (value && value[0]) {
            strlcat(summary, "\n- ", summary_size);
            strlcat(summary, value, summary_size);
        }
    }
}

bool tool_delegate_result_json_has_nonempty_evidence(const char *text)
{
    cJSON *root;
    cJSON *evidence;
    bool ok = false;

    if (!text || !text[0]) {
        return false;
    }

    root = cJSON_Parse(text);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }

    evidence = cJSON_GetObjectItem(root, "evidence");
    ok = evidence && cJSON_IsArray(evidence) && cJSON_GetArraySize(evidence) > 0;
    cJSON_Delete(root);
    return ok;
}

bool tool_delegate_parse_result_json_summary(const char *text, char *summary, size_t summary_size)
{
    if (!text || !text[0] || !summary || summary_size == 0) {
        return false;
    }

    cJSON *root = cJSON_Parse(text);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }

    const char *status = cJSON_GetStringValue(cJSON_GetObjectItem(root, "status"));
    const char *json_summary = cJSON_GetStringValue(cJSON_GetObjectItem(root, "summary"));
    cJSON *evidence = cJSON_GetObjectItem(root, "evidence");
    cJSON *risks = cJSON_GetObjectItem(root, "risks");
    cJSON *next_files = cJSON_GetObjectItem(root, "next_files");

    bool ok = status && strcmp(status, "done") == 0 &&
              json_summary && json_summary[0];
    if (!ok) {
        cJSON_Delete(root);
        return false;
    }

    summary[0] = '\0';
    strscpy(summary, json_summary, summary_size);
    append_string_array_section(summary, summary_size, evidence, "\n\nEvidence:");
    append_string_array_section(summary, summary_size, risks, "\n\nRisks:");
    append_string_array_section(summary, summary_size, next_files, "\n\nNext files:");

    cJSON_Delete(root);
    return true;
}

bool tool_delegate_parse_result_json_rendered(const char *text, char *summary, size_t summary_size)
{
    if (!text || !text[0] || !summary || summary_size == 0) {
        return false;
    }

    cJSON *root = cJSON_Parse(text);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }

    const char *status = cJSON_GetStringValue(cJSON_GetObjectItem(root, "status"));
    const char *json_summary = cJSON_GetStringValue(cJSON_GetObjectItem(root, "summary"));
    cJSON *evidence = cJSON_GetObjectItem(root, "evidence");
    cJSON *risks = cJSON_GetObjectItem(root, "risks");
    cJSON *next_files = cJSON_GetObjectItem(root, "next_files");

    if (!status || !status[0] || !json_summary || !json_summary[0]) {
        cJSON_Delete(root);
        return false;
    }

    summary[0] = '\0';
    strscpy(summary, json_summary, summary_size);
    append_string_array_section(summary, summary_size, evidence, "\n\nEvidence:");
    append_string_array_section(summary, summary_size, risks, "\n\nRisks:");
    append_string_array_section(summary, summary_size, next_files, "\n\nNext files:");

    cJSON_Delete(root);
    return true;
}

bool tool_delegate_extract_sync_final_output(const char *text,
                                             char *summary,
                                             size_t summary_size)
{
    if (!text || !text[0] || !summary || summary_size == 0) {
        return false;
    }

    cJSON *root = cJSON_Parse(text);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }

    const char *delivery = cJSON_GetStringValue(cJSON_GetObjectItem(root, "delivery"));
    const char *status = cJSON_GetStringValue(cJSON_GetObjectItem(root, "status"));
    const char *output = cJSON_GetStringValue(cJSON_GetObjectItem(root, "output"));
    bool ok = delivery && strcmp(delivery, "sync_final") == 0 &&
              status && strcmp(status, "done") == 0 &&
              output && output[0];
    if (ok) {
        strscpy(summary, output, summary_size);
    }

    cJSON_Delete(root);
    return ok;
}

bool tool_delegate_render_result_or_terminal_summary(const char *text,
                                                     char *summary,
                                                     size_t summary_size)
{
    return tool_delegate_parse_result_json_summary(text, summary, summary_size) ||
           tool_delegate_render_terminal_result_summary(text, summary, summary_size);
}
