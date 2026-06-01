#include "tools/tool_files.h"
#include "tools/tool_file_ops.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "cJSON.h"
#include "daima_config.h"
#include "daima_log.h"

typedef struct {
    char path[TOOL_FILES_PATH_SIZE];
    int offset;
    int limit;
    time_t mtime;
    bool valid;
} read_file_cache_t;

static const char *TAG = "tool_files";
static read_file_cache_t s_last_read = {0};

static bool read_file_should_dedup(const char *resolved_path, int offset, int limit, time_t mtime)
{
    return s_last_read.valid &&
           s_last_read.mtime == mtime &&
           s_last_read.offset == offset &&
           s_last_read.limit == limit &&
           strcmp(s_last_read.path, resolved_path) == 0;
}

static void remember_last_read(const char *resolved_path, int offset, int limit, time_t mtime)
{
    if (!resolved_path) return;
    snprintf(s_last_read.path, sizeof(s_last_read.path), "%s", resolved_path);
    s_last_read.offset = offset;
    s_last_read.limit = limit;
    s_last_read.mtime = mtime;
    s_last_read.valid = true;
}

daima_err_t tool_read_file_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "错误：输入 JSON 无效");
        return DAIMA_ERR_INVALID_ARG;
    }

    const char *path = cJSON_GetStringValue(cJSON_GetObjectItem(root, "path"));
    if (!path || !path[0]) {
        snprintf(output, output_size, "错误：缺少 'path' 字段");
        cJSON_Delete(root);
        return DAIMA_ERR_INVALID_ARG;
    }

    int offset = tool_files_clamp_int(
        tool_files_json_get_int_default(root, "offset", 1),
        1,
        1 << 20);
    int limit = tool_files_clamp_int(
        tool_files_json_get_int_default(root, "limit", DAIMA_READ_FILE_DEFAULT_LIMIT),
        1,
        DAIMA_READ_FILE_MAX_LIMIT);

    char resolved_path[TOOL_FILES_PATH_SIZE];
    if (!tool_files_resolve_read_path(path, resolved_path, sizeof(resolved_path))) {
        snprintf(output, output_size, "错误：无法解析路径：%s", path);
        cJSON_Delete(root);
        return DAIMA_ERR_INVALID_ARG;
    }

    struct stat st;
    if (stat(resolved_path, &st) != 0) {
        snprintf(output, output_size, "错误：文件不存在：%s", resolved_path);
        cJSON_Delete(root);
        return DAIMA_ERR_NOT_FOUND;
    }
    if (!S_ISREG(st.st_mode)) {
        snprintf(output, output_size, "错误：只支持读取普通文本文件：%s", resolved_path);
        cJSON_Delete(root);
        return DAIMA_ERR_INVALID_ARG;
    }

    if (read_file_should_dedup(resolved_path, offset, limit, st.st_mtime)) {
        snprintf(
            output, output_size,
            "提示：%s 在 offset=%d、limit=%d 的范围内自上次读取后没有变化。\n"
            "请直接复用上一条 read_file 结果，不必重复读取。",
            resolved_path, offset, limit);
        cJSON_Delete(root);
        return DAIMA_OK;
    }

    FILE *f = fopen(resolved_path, "r");
    if (!f) {
        snprintf(output, output_size, "错误：无法打开文件：%s", resolved_path);
        cJSON_Delete(root);
        return DAIMA_FAIL;
    }

    int total_lines = tool_files_count_total_lines(f);
    int requested_end = offset + limit - 1;
    int actual_end = total_lines > 0 ? requested_end : 0;
    if (total_lines > 0 && actual_end > total_lines) {
        actual_end = total_lines;
    }

    size_t char_budget = output_size > TOOL_FILES_READ_HEADER_RESERVE
                             ? output_size - TOOL_FILES_READ_HEADER_RESERVE
                             : output_size - 1;
    if (char_budget > DAIMA_READ_FILE_MAX_CHARS) {
        char_budget = DAIMA_READ_FILE_MAX_CHARS;
    }

    size_t off = snprintf(
        output, output_size,
        "FILE: %s\nLINES: %d-%d / %d\n\n",
        resolved_path,
        offset,
        actual_end >= offset ? actual_end : offset,
        total_lines);

    int current_line = 0;
    int emitted_lines = 0;
    bool page_truncated = false;
    bool char_truncated = false;
    char *line = NULL;
    size_t cap = 0;

    while (getline(&line, &cap, f) != -1) {
        current_line++;
        if (current_line < offset) {
            continue;
        }
        if (emitted_lines >= limit) {
            page_truncated = true;
            break;
        }

        tool_files_trim_line_end(line);

        char rendered[DAIMA_READ_FILE_MAX_LINE_CHARS + 64];
        if (strlen(line) > DAIMA_READ_FILE_MAX_LINE_CHARS) {
            snprintf(rendered, sizeof(rendered), "%6d|%.*s... [truncated]\n",
                     current_line, DAIMA_READ_FILE_MAX_LINE_CHARS, line);
        } else {
            snprintf(rendered, sizeof(rendered), "%6d|%s\n", current_line, line);
        }

        size_t rendered_len = strlen(rendered);
        if (off + rendered_len >= char_budget) {
            char_truncated = true;
            break;
        }

        memcpy(output + off, rendered, rendered_len);
        off += rendered_len;
        output[off] = '\0';
        emitted_lines++;
    }

    free(line);
    fclose(f);

    if (emitted_lines == 0 && total_lines > 0 && offset > total_lines) {
        off += snprintf(output + off, output_size - off, "（起始行超出文件总行数）\n");
    }

    int next_offset = offset + emitted_lines;
    if (char_truncated) {
        snprintf(
            output + off, output_size - off,
            "\n[Hint] 本次读取超过 %d 字符上限，已提前截断。请减小 limit，或从 offset=%d 继续读取。\n",
            DAIMA_READ_FILE_MAX_CHARS,
            next_offset > offset ? next_offset : offset);
    } else if (page_truncated || (total_lines > 0 && requested_end < total_lines)) {
        snprintf(
            output + off, output_size - off,
            "\n[Hint] 结果已分页。可用 offset=%d 继续读取。\n",
            requested_end + 1);
    }

    remember_last_read(resolved_path, offset, limit, st.st_mtime);

    DAIMA_LOGI(TAG, "read_file: %s lines=%d..%d/%d emitted=%d",
              resolved_path, offset, actual_end, total_lines, emitted_lines);
    cJSON_Delete(root);
    return DAIMA_OK;
}
