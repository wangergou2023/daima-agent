/* 文件修改类工具：edit_file / patch / restore_file。 */

#include "tools/tool_files.h"

#include "tools/tool_file_ops.h"
#include "app/daima_paths.h"
#include "daima_config.h"
#include "daima_log.h"
#include "cJSON.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

daima_err_t tool_edit_file_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "错误：输入 JSON 无效");
        return DAIMA_ERR_INVALID_ARG;
    }

    const char *path = cJSON_GetStringValue(cJSON_GetObjectItem(root, "path"));
    const char *old_str = cJSON_GetStringValue(cJSON_GetObjectItem(root, "old_string"));
    const char *new_str = cJSON_GetStringValue(cJSON_GetObjectItem(root, "new_string"));
    bool replace_all = tool_files_json_get_bool_default(root, "replace_all", false);

    char resolved_path[READ_PATH_SIZE];
    if (!resolve_write_path_or_fail(path, resolved_path, sizeof(resolved_path), output, output_size)) {
        cJSON_Delete(root);
        return DAIMA_ERR_INVALID_ARG;
    }
    if (!old_str || !new_str) {
        snprintf(output, output_size, "错误：缺少 'old_string' 或 'new_string' 字段");
        cJSON_Delete(root);
        return DAIMA_ERR_INVALID_ARG;
    }
    if (!old_str[0]) {
        snprintf(output, output_size, "错误：'old_string' 不能为空");
        cJSON_Delete(root);
        return DAIMA_ERR_INVALID_ARG;
    }

    char *current = NULL;
    size_t current_len = 0;
    daima_err_t err = tool_files_read_text_file(resolved_path, MAX_FILE_SIZE, &current, &current_len);
    if (err == DAIMA_ERR_NOT_FOUND) {
        snprintf(output, output_size, "错误：文件不存在：%s", resolved_path);
        cJSON_Delete(root);
        return DAIMA_ERR_NOT_FOUND;
    }
    if (err == DAIMA_ERR_INVALID_SIZE) {
        snprintf(output, output_size, "错误：文件过大或为空，无法编辑：%s", resolved_path);
        cJSON_Delete(root);
        return DAIMA_ERR_INVALID_SIZE;
    }
    if (err != DAIMA_OK) {
        snprintf(output, output_size, "错误：读取文件失败：%s", resolved_path);
        cJSON_Delete(root);
        return err;
    }

    size_t old_len = strlen(old_str);
    size_t new_len = strlen(new_str);
    char *result = NULL;
    size_t result_len = 0;
    int replaced_count = 0;
    err = tool_files_apply_replace(
        current,
        current_len,
        old_str,
        new_str,
        replace_all,
        &result,
        &result_len,
        &replaced_count,
        NULL);
    if (err == DAIMA_ERR_NOT_FOUND) {
        snprintf(output, output_size, "错误：在 %s 中未找到 old_string", resolved_path);
        free(current);
        cJSON_Delete(root);
        return DAIMA_ERR_NOT_FOUND;
    }
    if (err != DAIMA_OK) {
        snprintf(output, output_size, "错误：内存不足");
        free(current);
        cJSON_Delete(root);
        return err;
    }

    char checkpoint_path[READ_PATH_SIZE];
    checkpoint_path[0] = '\0';
    daima_err_t checkpoint_err = tool_files_checkpoint_before_write(
        resolved_path, current, current_len, checkpoint_path, sizeof(checkpoint_path));
    if (checkpoint_err != DAIMA_OK) {
        snprintf(output, output_size, "错误：编辑前创建检查点失败：%s", resolved_path);
        free(current);
        free(result);
        cJSON_Delete(root);
        return checkpoint_err;
    }

    if (tool_files_write_text_file(resolved_path, result, result_len) != DAIMA_OK) {
        snprintf(output, output_size, "错误：无法打开文件进行写入：%s", resolved_path);
        free(current);
        free(result);
        cJSON_Delete(root);
        return DAIMA_FAIL;
    }

    free(current);
    free(result);

    snprintf(output, output_size,
             "OK：已编辑 %s（替换 %d 处，将 %d bytes 替换为 %d bytes，checkpoint=%s）",
             resolved_path, replaced_count, (int)old_len, (int)new_len,
             checkpoint_path[0] ? checkpoint_path : "(none)");
    DAIMA_LOGI(TAG, "edit_file: %s replace_all=%d replaced=%d",
              resolved_path, replace_all ? 1 : 0, replaced_count);
    cJSON_Delete(root);
    return DAIMA_OK;
}

daima_err_t tool_patch_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    if (!root || !cJSON_IsObject(root)) {
        snprintf(output, output_size, "错误：输入 JSON 无效");
        cJSON_Delete(root);
        return DAIMA_ERR_INVALID_ARG;
    }

    const char *path = cJSON_GetStringValue(cJSON_GetObjectItem(root, "path"));
    cJSON *edits = cJSON_GetObjectItem(root, "edits");
    bool preview = tool_files_json_get_bool_default(root, "preview", false);

    char resolved_path[READ_PATH_SIZE];
    if (!resolve_write_path_or_fail(path, resolved_path, sizeof(resolved_path), output, output_size)) {
        cJSON_Delete(root);
        return DAIMA_ERR_INVALID_ARG;
    }
    if (!edits || !cJSON_IsArray(edits) || cJSON_GetArraySize(edits) <= 0) {
        snprintf(output, output_size, "错误：缺少非空 edits 数组");
        cJSON_Delete(root);
        return DAIMA_ERR_INVALID_ARG;
    }

    char *current = NULL;
    size_t current_len = 0;
    daima_err_t err = tool_files_read_text_file(resolved_path, MAX_FILE_SIZE, &current, &current_len);
    if (err != DAIMA_OK) {
        if (err == DAIMA_ERR_NOT_FOUND) {
            snprintf(output, output_size, "错误：文件不存在：%s", resolved_path);
        } else if (err == DAIMA_ERR_INVALID_SIZE) {
            snprintf(output, output_size, "错误：文件过大或为空，无法 patch：%s", resolved_path);
        } else {
            snprintf(output, output_size, "错误：读取文件失败：%s", resolved_path);
        }
        cJSON_Delete(root);
        return err;
    }

    int total_replaced = 0;
    int applied_steps = 0;
    size_t preview_off = 0;
    preview_off += snprintf(
        output + preview_off,
        output_size - preview_off,
        "PATCH %s\nFILE: %s\n\n",
        preview ? "PREVIEW" : "PLAN",
        resolved_path);
    cJSON *edit = NULL;
    cJSON_ArrayForEach(edit, edits) {
        if (!cJSON_IsObject(edit)) {
            snprintf(output, output_size, "错误：edits[%d] 不是对象", applied_steps);
            err = DAIMA_ERR_INVALID_ARG;
            goto patch_cleanup;
        }

        const char *old_str = cJSON_GetStringValue(cJSON_GetObjectItem(edit, "old_string"));
        const char *new_str = cJSON_GetStringValue(cJSON_GetObjectItem(edit, "new_string"));
        bool replace_all = tool_files_json_get_bool_default(edit, "replace_all", false);
        if (!old_str || !new_str || !old_str[0]) {
            snprintf(output, output_size, "错误：edits[%d] 缺少有效 old_string/new_string", applied_steps);
            err = DAIMA_ERR_INVALID_ARG;
            goto patch_cleanup;
        }

        char *next = NULL;
        size_t next_len = 0;
        int replaced_count = 0;
        size_t first_match_offset = (size_t)-1;
        err = tool_files_apply_replace(
            current,
            current_len,
            old_str,
            new_str,
            replace_all,
            &next,
            &next_len,
            &replaced_count,
            &first_match_offset);
        if (err != DAIMA_OK) {
            if (err == DAIMA_ERR_NOT_FOUND) {
                snprintf(output, output_size, "错误：edits[%d] 在 %s 中未找到 old_string", applied_steps, resolved_path);
            } else {
                snprintf(output, output_size, "错误：edits[%d] patch 失败", applied_steps);
            }
            free(next);
            goto patch_cleanup;
        }

        char before_snippet[160];
        char after_snippet[160];
        tool_files_build_patch_preview_snippet(current, current_len, first_match_offset, strlen(old_str), before_snippet, sizeof(before_snippet));
        tool_files_build_patch_preview_snippet(next, next_len, first_match_offset, strlen(new_str), after_snippet, sizeof(after_snippet));
        if (preview_off < output_size - 1) {
            preview_off += snprintf(
                output + preview_off,
                output_size - preview_off,
                "- edit[%d]: replace_all=%d replaced=%d\n  old: %s\n  new: %s\n  before: %s\n  after: %s\n",
                applied_steps,
                replace_all ? 1 : 0,
                replaced_count,
                old_str,
                new_str,
                before_snippet[0] ? before_snippet : "(empty)",
                after_snippet[0] ? after_snippet : "(empty)");
        }

        free(current);
        current = next;
        current_len = next_len;
        total_replaced += replaced_count;
        applied_steps++;
    }

    if (preview) {
        snprintf(
            output + preview_off,
            output_size - preview_off,
            "\nSUMMARY: %d edits, %d replacements, final_size=%d bytes\n[Hint] 这是预览，文件尚未写回。去掉 preview 或设为 false 才会真正修改文件。\n",
            applied_steps,
            total_replaced,
            (int)current_len);
        err = DAIMA_OK;
        DAIMA_LOGI(TAG, "patch preview: %s edits=%d replaced=%d", resolved_path, applied_steps, total_replaced);
        goto patch_cleanup;
    }

    char checkpoint_path[READ_PATH_SIZE];
    checkpoint_path[0] = '\0';
    err = tool_files_checkpoint_current_file(
        resolved_path, MAX_FILE_SIZE, checkpoint_path, sizeof(checkpoint_path));
    if (err != DAIMA_OK) {
        snprintf(output, output_size, "错误：patch 前创建检查点失败：%s", resolved_path);
        goto patch_cleanup;
    }

    err = tool_files_write_text_file(resolved_path, current, current_len);
    if (err != DAIMA_OK) {
        snprintf(output, output_size, "错误：写回文件失败：%s", resolved_path);
        goto patch_cleanup;
    }

    snprintf(
        output,
        output_size,
        "OK：已 patch %s（%d 个 edits，累计替换 %d 处，checkpoint=%s）",
        resolved_path,
        applied_steps,
        total_replaced,
        checkpoint_path[0] ? checkpoint_path : "(none)");
    DAIMA_LOGI(TAG, "patch: %s edits=%d replaced=%d", resolved_path, applied_steps, total_replaced);

patch_cleanup:
    free(current);
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
