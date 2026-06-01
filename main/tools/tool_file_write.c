#include "tools/tool_files.h"
#include "tools/tool_file_ops.h"
#include "app/daima_paths.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "daima_config.h"
#include "daima_log.h"

static const char *TAG = "tool_files";

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

daima_err_t tool_write_file_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "错误：输入 JSON 无效");
        return DAIMA_ERR_INVALID_ARG;
    }

    const char *path = cJSON_GetStringValue(cJSON_GetObjectItem(root, "path"));
    const char *content = cJSON_GetStringValue(cJSON_GetObjectItem(root, "content"));

    char resolved_path[TOOL_FILES_PATH_SIZE];
    if (!resolve_write_path_or_fail(path, resolved_path, sizeof(resolved_path), output, output_size)) {
        cJSON_Delete(root);
        return DAIMA_ERR_INVALID_ARG;
    }
    if (!content) {
        snprintf(output, output_size, "错误：缺少 'content' 字段");
        cJSON_Delete(root);
        return DAIMA_ERR_INVALID_ARG;
    }

    tool_files_ensure_parent_dirs(resolved_path);

    size_t len = strlen(content);
    char checkpoint_path[TOOL_FILES_PATH_SIZE];
    checkpoint_path[0] = '\0';

    daima_err_t checkpoint_err = tool_files_checkpoint_current_file(
        resolved_path, TOOL_FILES_MAX_FILE_SIZE, checkpoint_path, sizeof(checkpoint_path));
    if (checkpoint_err != DAIMA_OK) {
        snprintf(output, output_size, "错误：写入前创建检查点失败：%s", resolved_path);
        cJSON_Delete(root);
        return checkpoint_err;
    }

    if (tool_files_write_text_file(resolved_path, content, len) != DAIMA_OK) {
        snprintf(output, output_size, "错误：写入 %s 失败", resolved_path);
        cJSON_Delete(root);
        return DAIMA_FAIL;
    }

    if (checkpoint_path[0]) {
        snprintf(output, output_size, "OK：已写入 %d bytes 到 %s（checkpoint=%s）", (int)len, resolved_path, checkpoint_path);
    } else {
        snprintf(output, output_size, "OK：已写入 %d bytes 到 %s", (int)len, resolved_path);
    }
    DAIMA_LOGI(TAG, "write_file: %s (%d bytes)", resolved_path, (int)len);
    cJSON_Delete(root);
    return DAIMA_OK;
}
