/* 文件修改类工具：apply_patch / restore_file。 */

#include "tools/tool_files.h"

#include "tools/tool_file_ops.h"
#include "tools/tool_hashline.h"
#include "tools/tool_safe_edit.h"
#include "app/daima_paths.h"
#include "daima_config.h"
#include "daima_log.h"
#include "cJSON.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *TAG = "tool_files";

#define MAX_FILE_SIZE (32 * 1024)
#define READ_PATH_SIZE 1024

static bool resolve_write_path_or_fail(const char *path,
                                       char *resolved_path,
                                       size_t resolved_path_size,
                                       char *output,
                                       size_t output_size)
{
    if (tool_files_resolve_write_path(path, resolved_path, resolved_path_size)) {
        return true;
    }
    snprintf(output, output_size, "错误：只允许修改当前工作目录或 %s 下的路径，且不能包含 '..'", daima_path_spiffs_base());
    return false;
}

static bool resolve_new_file_path_or_fail(const char *path,
                                          char *resolved_path,
                                          size_t resolved_path_size,
                                          char *output,
                                          size_t output_size)
{
    if (resolve_write_path_or_fail(path, resolved_path, resolved_path_size, output, output_size)) {
        return true;
    }
    if (!path || !path[0] || path[0] == '/' || strstr(path, "..") != NULL) {
        return false;
    }
    if (snprintf(resolved_path, resolved_path_size, "%s/%s", daima_path_workspace_dir(), path) >= (int)resolved_path_size) {
        snprintf(output, output_size, "错误：路径过长：%s", path);
        return false;
    }
    return true;
}

static bool path_has_prefix(const char *path, const char *prefix)
{
    if (!path || !prefix) {
        return false;
    }
    size_t prefix_len = strlen(prefix);
    if (strncmp(path, prefix, prefix_len) != 0) {
        return false;
    }
    return path[prefix_len] == '\0' || path[prefix_len] == '/';
}

static char *next_patch_line(char **cursor);

#ifdef DAIMA_HASHLINE_ENABLED
static bool parse_hashline_reference(const char *line, int *line_number, char expected_hash[5])
{
    if (!line || !isdigit((unsigned char)line[0]) || !line_number || !expected_hash) {
        return false;
    }

    int parsed_line = 0;
    const char *p = line;
    while (isdigit((unsigned char)*p)) {
        parsed_line = parsed_line * 10 + (*p - '0');
        p++;
    }
    if (parsed_line <= 0 || *p != '#') {
        return false;
    }
    p++;
    for (int i = 0; i < 4; i++) {
        if (!((p[i] >= '0' && p[i] <= '9') || (p[i] >= 'a' && p[i] <= 'f'))) {
            return false;
        }
        expected_hash[i] = p[i];
    }
    expected_hash[4] = '\0';
    if (p[4] != '|') {
        return false;
    }

    *line_number = parsed_line;
    return true;
}

static bool read_line_without_newline(const char *content, int wanted_line, char **line_out)
{
    if (!content || wanted_line <= 0 || !line_out) {
        return false;
    }

    const char *line_start = content;
    int current_line = 1;
    while (current_line < wanted_line && *line_start) {
        const char *nl = strchr(line_start, '\n');
        if (!nl) {
            return false;
        }
        line_start = nl + 1;
        current_line++;
    }
    if (current_line != wanted_line || !*line_start) {
        return false;
    }

    const char *line_end = strchr(line_start, '\n');
    if (!line_end) {
        line_end = line_start + strlen(line_start);
    }
    while (line_end > line_start && line_end[-1] == '\r') {
        line_end--;
    }

    size_t len = (size_t)(line_end - line_start);
    char *copy = malloc(len + 1);
    if (!copy) {
        return false;
    }
    memcpy(copy, line_start, len);
    copy[len] = '\0';
    *line_out = copy;
    return true;
}

static daima_err_t verify_hashline_reference(const char *current,
                                             const char *line,
                                             bool *saw_hashline,
                                             char *output,
                                             size_t output_size)
{
    int line_number = 0;
    char expected_hash[5] = {0};
    if (!parse_hashline_reference(line, &line_number, expected_hash)) {
        return DAIMA_OK;
    }
    if (saw_hashline) {
        *saw_hashline = true;
    }

    char *actual_line = NULL;
    if (!read_line_without_newline(current, line_number, &actual_line)) {
        snprintf(output, output_size,
                 "Hashline: 第%d行不存在，请重新读取文件后重试",
                 line_number);
        return DAIMA_ERR_INVALID_STATE;
    }

    bool matched = hashline_verify_line(line_number, actual_line, expected_hash);
    free(actual_line);
    if (!matched) {
        snprintf(output, output_size,
                 "Hashline: 第%d行已变更(hash期望%s)，请重新读取文件后重试",
                 line_number, expected_hash);
        return DAIMA_ERR_INVALID_STATE;
    }
    return DAIMA_OK;
}

static daima_err_t hashline_verify_patch_path(const char *path,
                                              const char *patch_content,
                                              bool *saw_hashline,
                                              char *output,
                                              size_t output_size)
{
    if (saw_hashline) {
        *saw_hashline = false;
    }

    char resolved_path[READ_PATH_SIZE];
    char scratch[128];
    if (!resolve_write_path_or_fail(path, resolved_path, sizeof(resolved_path), scratch, sizeof(scratch))) {
        return DAIMA_OK;
    }

    char *current = NULL;
    size_t current_len = 0;
    daima_err_t err = tool_files_read_text_file(resolved_path, MAX_FILE_SIZE, &current, &current_len);
    if (err != DAIMA_OK) {
        return DAIMA_OK;
    }
    (void)current_len;

    char *copy = strdup(patch_content ? patch_content : "");
    if (!copy) {
        free(current);
        snprintf(output, output_size, "错误：内存不足");
        return DAIMA_ERR_NO_MEM;
    }

    char *cursor = copy;
    char *line = NULL;
    while ((line = next_patch_line(&cursor)) != NULL) {
        if (line[0] == '-' || line[0] == ' ') {
            err = verify_hashline_reference(current, line + 1, saw_hashline, output, output_size);
            if (err != DAIMA_OK) {
                free(copy);
                free(current);
                return err;
            }
        }
    }

    free(copy);
    free(current);
    return DAIMA_OK;
}
#endif

static daima_err_t safe_edit_verify_patch_path(const char *path,
                                               const char *patch_content,
                                               char *output,
                                               size_t output_size)
{
#ifdef DAIMA_HASHLINE_ENABLED
    bool saw_hashline = false;
    daima_err_t hashline_err = hashline_verify_patch_path(path, patch_content, &saw_hashline, output, output_size);
    if (hashline_err != DAIMA_OK || saw_hashline) {
        return hashline_err;
    }
#endif
#ifdef DAIMA_SAFE_EDIT_ENABLED
    char resolved_path[READ_PATH_SIZE];
    char scratch[128];
    if (!resolve_write_path_or_fail(path, resolved_path, sizeof(resolved_path), scratch, sizeof(scratch))) {
        return DAIMA_OK;
    }
    if (safe_edit_verify(resolved_path, patch_content) != DAIMA_OK) {
        snprintf(output, output_size,
                 "SafeEdit: 文件自上次读取后已被修改，请重新读取后再编辑");
        return DAIMA_ERR_INVALID_STATE;
    }
#else
    (void)path;
    (void)patch_content;
    (void)output;
    (void)output_size;
#endif
    return DAIMA_OK;
}

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} text_builder_t;

static daima_err_t tb_append(text_builder_t *tb, const char *text, size_t len)
{
    if (!tb || (!text && len > 0)) {
        return DAIMA_ERR_INVALID_ARG;
    }
    if (tb->len + len + 1 > tb->cap) {
        size_t next_cap = tb->cap ? tb->cap : 256;
        while (next_cap < tb->len + len + 1) {
            next_cap *= 2;
        }
        char *next = realloc(tb->data, next_cap);
        if (!next) {
            return DAIMA_ERR_NO_MEM;
        }
        tb->data = next;
        tb->cap = next_cap;
    }
    if (len > 0) {
        memcpy(tb->data + tb->len, text, len);
        tb->len += len;
    }
    tb->data[tb->len] = '\0';
    return DAIMA_OK;
}

static daima_err_t tb_append_line(text_builder_t *tb, const char *line)
{
    daima_err_t err = tb_append(tb, line ? line : "", line ? strlen(line) : 0);
    if (err != DAIMA_OK) {
        return err;
    }
    return tb_append(tb, "\n", 1);
}

static bool starts_with(const char *text, const char *prefix)
{
    return text && prefix && strncmp(text, prefix, strlen(prefix)) == 0;
}

static char *next_patch_line(char **cursor)
{
    if (!cursor || !*cursor) {
        return NULL;
    }
    char *line = *cursor;
    char *nl = strchr(line, '\n');
    if (nl) {
        *nl = '\0';
        *cursor = nl + 1;
    } else {
        *cursor = NULL;
    }
    size_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\r') {
        line[len - 1] = '\0';
    }
    return line;
}

static const char *require_patch_path(const char *line, const char *prefix)
{
    if (!starts_with(line, prefix)) {
        return NULL;
    }
    const char *path = line + strlen(prefix);
    return path[0] ? path : NULL;
}

static daima_err_t apply_patch_add_file(char **cursor,
                                        const char *path,
                                        char *output,
                                        size_t output_size)
{
    char resolved_path[READ_PATH_SIZE];
    if (!resolve_new_file_path_or_fail(path, resolved_path, sizeof(resolved_path), output, output_size)) {
        return DAIMA_ERR_INVALID_ARG;
    }
    if (access(resolved_path, F_OK) == 0) {
        snprintf(output, output_size, "错误：Add File 目标已存在：%s", resolved_path);
        return DAIMA_ERR_INVALID_STATE;
    }

    text_builder_t content = {0};
    char *line = NULL;
    int non_prefixed_count = 0;
    char first_non_prefixed[96] = {0};
    while ((line = next_patch_line(cursor)) != NULL) {
        if (strcmp(line, "*** End Patch") == 0) {
            tool_files_ensure_parent_dirs(resolved_path);
            daima_err_t err = tool_files_write_text_file(resolved_path, content.data ? content.data : "", content.len);
            free(content.data);
            if (err != DAIMA_OK) {
                snprintf(output, output_size, "错误：Add File 写入失败：%s", resolved_path);
                return err;
            }
            snprintf(output, output_size, "OK：apply_patch 已新增 %s（%d bytes）", resolved_path, (int)content.len);
            DAIMA_LOGI(TAG, "apply_patch add: %s bytes=%d", resolved_path, (int)content.len);
            if (non_prefixed_count > 0) {
                DAIMA_LOGI(TAG,
                           "apply_patch add accepted %d non-prefixed content lines: path=%s first=%.80s",
                           non_prefixed_count,
                           resolved_path,
                           first_non_prefixed);
            }
            return DAIMA_OK;
        }
        const char *content_line = line;
        if (starts_with(line, "+")) {
            content_line = line + 1;
        } else {
            if (non_prefixed_count == 0) {
                snprintf(first_non_prefixed, sizeof(first_non_prefixed), "%s", line);
            }
            non_prefixed_count++;
        }
        daima_err_t err = tb_append_line(&content, content_line);
        if (err != DAIMA_OK) {
            free(content.data);
            snprintf(output, output_size, "错误：内存不足");
            return err;
        }
    }
    free(content.data);
    snprintf(output, output_size, "错误：patch 缺少 *** End Patch");
    return DAIMA_ERR_INVALID_ARG;
}

static daima_err_t apply_patch_delete_file(const char *path,
                                           char *output,
                                           size_t output_size)
{
    char resolved_path[READ_PATH_SIZE];
    if (!resolve_write_path_or_fail(path, resolved_path, sizeof(resolved_path), output, output_size)) {
        return DAIMA_ERR_INVALID_ARG;
    }

    char checkpoint_path[READ_PATH_SIZE];
    checkpoint_path[0] = '\0';
    daima_err_t err = tool_files_checkpoint_current_file(
        resolved_path, MAX_FILE_SIZE, checkpoint_path, sizeof(checkpoint_path));
    if (err != DAIMA_OK) {
        snprintf(output, output_size, "错误：Delete File 前创建检查点失败：%s", resolved_path);
        return err;
    }
    if (unlink(resolved_path) != 0) {
        snprintf(output, output_size, "错误：Delete File 删除失败：%s", resolved_path);
        return DAIMA_FAIL;
    }
    snprintf(output, output_size, "OK：apply_patch 已删除 %s（checkpoint=%s）",
             resolved_path, checkpoint_path[0] ? checkpoint_path : "(none)");
    DAIMA_LOGI(TAG, "apply_patch delete: %s", resolved_path);
    return DAIMA_OK;
}

static daima_err_t apply_patch_update_file(char **cursor,
                                           const char *path,
                                           char *output,
                                           size_t output_size)
{
    char resolved_path[READ_PATH_SIZE];
    if (!resolve_write_path_or_fail(path, resolved_path, sizeof(resolved_path), output, output_size)) {
        return DAIMA_ERR_INVALID_ARG;
    }

    char *current = NULL;
    size_t current_len = 0;
    daima_err_t err = tool_files_read_text_file(resolved_path, MAX_FILE_SIZE, &current, &current_len);
    if (err != DAIMA_OK) {
        snprintf(output, output_size, "错误：Update File 读取失败：%s", resolved_path);
        return err;
    }

    text_builder_t old_text = {0};
    text_builder_t new_text = {0};
    bool saw_hunk = false;
    char *line = NULL;
    while ((line = next_patch_line(cursor)) != NULL) {
        if (strcmp(line, "*** End Patch") == 0) {
            break;
        }
        if (strcmp(line, "@@") == 0 || starts_with(line, "@@ ")) {
            saw_hunk = true;
            continue;
        }
        if (!saw_hunk) {
            free(current);
            free(old_text.data);
            free(new_text.data);
            snprintf(output, output_size, "错误：Update File 缺少 @@ hunk");
            return DAIMA_ERR_INVALID_ARG;
        }

        if (starts_with(line, "-")) {
            err = tb_append_line(&old_text, hashline_strip_prefix(line + 1));
        } else if (starts_with(line, "+")) {
            err = tb_append_line(&new_text, hashline_strip_prefix(line + 1));
        } else if (starts_with(line, " ")) {
            err = tb_append_line(&old_text, hashline_strip_prefix(line + 1));
            if (err == DAIMA_OK) {
                err = tb_append_line(&new_text, hashline_strip_prefix(line + 1));
            }
        } else {
            free(current);
            free(old_text.data);
            free(new_text.data);
            snprintf(output, output_size, "错误：Update File hunk 行必须以空格、'-' 或 '+' 开头");
            return DAIMA_ERR_INVALID_ARG;
        }
        if (err != DAIMA_OK) {
            free(current);
            free(old_text.data);
            free(new_text.data);
            snprintf(output, output_size, "错误：内存不足");
            return err;
        }
    }
    if (!line) {
        free(current);
        free(old_text.data);
        free(new_text.data);
        snprintf(output, output_size, "错误：patch 缺少 *** End Patch");
        return DAIMA_ERR_INVALID_ARG;
    }
    if (!saw_hunk || old_text.len == 0) {
        free(current);
        free(old_text.data);
        free(new_text.data);
        snprintf(output, output_size, "错误：Update File hunk 为空");
        return DAIMA_ERR_INVALID_ARG;
    }

    char *result = NULL;
    size_t result_len = 0;
    int replaced_count = 0;
    err = tool_files_apply_replace(
        current,
        current_len,
        old_text.data,
        new_text.data ? new_text.data : "",
        false,
        &result,
        &result_len,
        &replaced_count,
        NULL);
    if (err != DAIMA_OK) {
        snprintf(output, output_size, "错误：Update File 在 %s 中未找到 hunk 上下文", resolved_path);
        free(current);
        free(old_text.data);
        free(new_text.data);
        free(result);
        return err;
    }

    char checkpoint_path[READ_PATH_SIZE];
    checkpoint_path[0] = '\0';
    err = tool_files_checkpoint_before_write(
        resolved_path, current, current_len, checkpoint_path, sizeof(checkpoint_path));
    if (err == DAIMA_OK) {
        err = tool_files_write_text_file(resolved_path, result, result_len);
    }

    free(current);
    free(old_text.data);
    free(new_text.data);
    free(result);
    if (err != DAIMA_OK) {
        snprintf(output, output_size, "错误：Update File 写回失败：%s", resolved_path);
        return err;
    }

    snprintf(output, output_size, "OK：apply_patch 已更新 %s（替换 %d 处，checkpoint=%s）",
             resolved_path, replaced_count, checkpoint_path[0] ? checkpoint_path : "(none)");
    DAIMA_LOGI(TAG, "apply_patch update: %s replaced=%d", resolved_path, replaced_count);
    return DAIMA_OK;
}

daima_err_t tool_apply_patch_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    if (!root || !cJSON_IsObject(root)) {
        snprintf(output, output_size, "错误：输入 JSON 无效");
        cJSON_Delete(root);
        return DAIMA_ERR_INVALID_ARG;
    }

    const char *patch = cJSON_GetStringValue(cJSON_GetObjectItem(root, "patch"));
    if (!patch || !patch[0]) {
        snprintf(output, output_size, "错误：缺少 patch 字段");
        cJSON_Delete(root);
        return DAIMA_ERR_INVALID_ARG;
    }

    char *copy = strdup(patch);
    if (!copy) {
        snprintf(output, output_size, "错误：内存不足");
        cJSON_Delete(root);
        return DAIMA_ERR_NO_MEM;
    }

    char *cursor = copy;
    char *line = next_patch_line(&cursor);
    if (!line || strcmp(line, "*** Begin Patch") != 0) {
        snprintf(output, output_size, "错误：patch 必须以 *** Begin Patch 开始");
        free(copy);
        cJSON_Delete(root);
        return DAIMA_ERR_INVALID_ARG;
    }

    daima_err_t err = DAIMA_ERR_INVALID_ARG;
    line = next_patch_line(&cursor);
    const char *path = require_patch_path(line, "*** Add File: ");
    if (path) {
        err = apply_patch_add_file(&cursor, path, output, output_size);
    } else if ((path = require_patch_path(line, "*** Update File: ")) != NULL) {
        err = safe_edit_verify_patch_path(path, patch, output, output_size);
        if (err == DAIMA_OK) {
            err = apply_patch_update_file(&cursor, path, output, output_size);
        }
    } else if ((path = require_patch_path(line, "*** Delete File: ")) != NULL) {
        err = safe_edit_verify_patch_path(path, patch, output, output_size);
        if (err == DAIMA_OK) {
            line = next_patch_line(&cursor);
            if (!line || strcmp(line, "*** End Patch") != 0) {
                snprintf(output, output_size, "错误：Delete File 后必须直接结束 patch");
                err = DAIMA_ERR_INVALID_ARG;
            } else {
                err = apply_patch_delete_file(path, output, output_size);
            }
        }
    } else {
        snprintf(output, output_size, "错误：patch 必须包含 Add File、Update File 或 Delete File");
    }

    free(copy);
    cJSON_Delete(root);
    return err;
}

daima_err_t tool_restore_file_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    if (!root || !cJSON_IsObject(root)) {
        snprintf(output, output_size, "错误：输入 JSON 无效");
        cJSON_Delete(root);
        return DAIMA_ERR_INVALID_ARG;
    }

    const char *path = cJSON_GetStringValue(cJSON_GetObjectItem(root, "path"));
    const char *checkpoint_hint = cJSON_GetStringValue(cJSON_GetObjectItem(root, "checkpoint_path"));
    if (!path || !path[0]) {
        snprintf(output, output_size, "错误：缺少 path");
        cJSON_Delete(root);
        return DAIMA_ERR_INVALID_ARG;
    }

    char resolved_path[READ_PATH_SIZE];
    if (!resolve_write_path_or_fail(path, resolved_path, sizeof(resolved_path), output, output_size)) {
        cJSON_Delete(root);
        return DAIMA_ERR_INVALID_ARG;
    }

    char checkpoint_path[READ_PATH_SIZE];
    checkpoint_path[0] = '\0';
    if (checkpoint_hint && checkpoint_hint[0]) {
        if (!tool_files_resolve_read_path(checkpoint_hint, checkpoint_path, sizeof(checkpoint_path)) ||
            !path_has_prefix(checkpoint_path, daima_path_checkpoint_dir())) {
            snprintf(output, output_size, "错误：checkpoint_path 必须位于 %s 下", daima_path_checkpoint_dir());
            cJSON_Delete(root);
            return DAIMA_ERR_INVALID_ARG;
        }
    } else if (!tool_files_get_recent_checkpoint(resolved_path, checkpoint_path, sizeof(checkpoint_path))) {
        snprintf(output, output_size, "错误：未找到 %s 的最近检查点", resolved_path);
        cJSON_Delete(root);
        return DAIMA_ERR_NOT_FOUND;
    }

    char rollback_checkpoint[READ_PATH_SIZE];
    rollback_checkpoint[0] = '\0';
    daima_err_t err = tool_files_restore_checkpoint(
        resolved_path,
        checkpoint_path,
        MAX_FILE_SIZE,
        rollback_checkpoint,
        sizeof(rollback_checkpoint));
    cJSON_Delete(root);
    if (err != DAIMA_OK) {
        snprintf(output, output_size, "错误：恢复失败：%s", resolved_path);
        return err;
    }

    snprintf(output, output_size,
             "OK：已将 %s 恢复到 checkpoint=%s（恢复前当前版本已保存到 %s）",
             resolved_path, checkpoint_path, rollback_checkpoint[0] ? rollback_checkpoint : "(none)");
    DAIMA_LOGI(TAG, "restore_file: %s <- %s", resolved_path, checkpoint_path);
    return DAIMA_OK;
}
