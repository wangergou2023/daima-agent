#include "drivers/tool/tool_files.h"
#include "drivers/tool/tool_file_list.h"
#include "drivers/tool/tool_file_ops.h"
#include "paths.h"
#include "drivers/tool/tool_file_search.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "cJSON.h"
#include "autoconf.h"
#include "linux/printk.h"
daima_err_t tool_list_dir_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    const char *prefix = NULL;
    const char *path = NULL;
    if (root) {
        cJSON *pfx = cJSON_GetObjectItem(root, "prefix");
        if (pfx && cJSON_IsString(pfx)) {
            prefix = pfx->valuestring;
        }
        cJSON *dir_path = cJSON_GetObjectItem(root, "path");
        if (dir_path && cJSON_IsString(dir_path)) {
            path = dir_path->valuestring;
        }
    }

    char resolved_dir[TOOL_FILES_PATH_SIZE];
    if (!tool_files_resolve_list_dir_path(path && path[0] ? path : ".", resolved_dir, sizeof(resolved_dir))) {
        snprintf(output, output_size, "错误：只允许列出当前工作目录或 %s 下的目录，且不能包含 '..'", daima_path_spiffs_base());
        cJSON_Delete(root);
        return DAIMA_ERR_INVALID_ARG;
    }

    int count = 0;
    daima_err_t err = tool_files_list_dir(resolved_dir, prefix, output, output_size, &count);
    if (err != DAIMA_OK) {
        snprintf(output, output_size, "错误：无法打开目录 %s", resolved_dir);
        cJSON_Delete(root);
        return err;
    }

    if (count == 0) {
        snprintf(output, output_size, "（未找到文件）");
    }

    pr_info("list_dir: %d files (path=%s prefix=%s)", count, resolved_dir, prefix ? prefix : "(none)");
    cJSON_Delete(root);
    return DAIMA_OK;
}

daima_err_t tool_search_files_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "错误：输入 JSON 无效");
        return DAIMA_ERR_INVALID_ARG;
    }

    const char *pattern = cJSON_GetStringValue(cJSON_GetObjectItem(root, "pattern"));
    const char *target = cJSON_GetStringValue(cJSON_GetObjectItem(root, "target"));
    const char *path = cJSON_GetStringValue(cJSON_GetObjectItem(root, "path"));
    const char *file_glob = cJSON_GetStringValue(cJSON_GetObjectItem(root, "file_glob"));
    const char *output_mode = cJSON_GetStringValue(cJSON_GetObjectItem(root, "output_mode"));
    int context = tool_files_clamp_int(
        tool_files_json_get_int_default(root, "context", 0),
        0,
        DAIMA_SEARCH_FILES_MAX_CONTEXT);
    int offset = tool_files_clamp_int(
        tool_files_json_get_int_default(root, "offset", 0),
        0,
        1 << 20);
    int limit = tool_files_clamp_int(
        tool_files_json_get_int_default(root, "limit", DAIMA_SEARCH_FILES_DEFAULT_LIMIT),
        1,
        DAIMA_SEARCH_FILES_MAX_LIMIT);

    if (!pattern || !pattern[0]) {
        snprintf(output, output_size, "错误：缺少 'pattern' 字段");
        cJSON_Delete(root);
        return DAIMA_ERR_INVALID_ARG;
    }

    char resolved_dir[TOOL_FILES_PATH_SIZE];
    if (!tool_files_resolve_list_dir_path(path && path[0] ? path : ".", resolved_dir, sizeof(resolved_dir))) {
        snprintf(output, output_size, "错误：只允许搜索当前工作目录或 %s 下的目录，且不能包含 '..'", daima_path_spiffs_base());
        cJSON_Delete(root);
        return DAIMA_ERR_INVALID_ARG;
    }

    struct stat st;
    if (stat(resolved_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        snprintf(output, output_size, "错误：搜索起点不是目录：%s", resolved_dir);
        cJSON_Delete(root);
        return DAIMA_ERR_INVALID_ARG;
    }

    bool search_files_only = target && strcmp(target, "files") == 0;
    if (!output_mode || !output_mode[0]) {
        output_mode = search_files_only ? "files_only" : "content";
    }
    if (strcmp(output_mode, "content") != 0 &&
        strcmp(output_mode, "files_only") != 0 &&
        strcmp(output_mode, "count") != 0) {
        snprintf(output, output_size, "错误：output_mode 只支持 content / files_only / count");
        cJSON_Delete(root);
        return DAIMA_ERR_INVALID_ARG;
    }
    if (search_files_only || strcmp(output_mode, "content") != 0) {
        context = 0;
    }

    search_ctx_t ctx = {
        .pattern = pattern,
        .file_glob = file_glob,
        .output_mode = output_mode,
        .search_files_only = search_files_only,
        .context = context,
        .offset = offset,
        .limit = limit,
        .count = 0,
        .seen = 0,
        .truncated = false,
        .off = 0,
        .output = output,
        .output_size = output_size,
    };

    snprintf(
        output, output_size,
        "SEARCH: %s\nTARGET: %s\nOUTPUT_MODE: %s\nPATH: %s\nOFFSET: %d\nLIMIT: %d\nCONTEXT: %d\n\n",
        pattern,
        search_files_only ? "files" : "content",
        output_mode,
        resolved_dir,
        offset,
        limit,
        context);
    ctx.off = strlen(output);

    tool_files_search_dir_recursive(resolved_dir, &ctx);

    if (ctx.count == 0) {
        snprintf(output + ctx.off, output_size - ctx.off, "（未找到结果）\n");
    } else if (ctx.truncated || ctx.count >= limit) {
        snprintf(output + ctx.off, output_size - ctx.off,
                 "\n[Hint] 结果已截断。可用 offset=%d 继续查看下一页，或缩小 path / file_glob。\n",
                 offset + ctx.count);
    }

    pr_info("search_files: pattern=%s target=%s mode=%s path=%s offset=%d limit=%d context=%d count=%d seen=%d", pattern, search_files_only ? "files" : "content", output_mode, resolved_dir, offset, limit, context, ctx.count, ctx.seen);
    cJSON_Delete(root);
    return DAIMA_OK;
}
