/* 文件搜索辅助层：递归搜索文件名或文本内容。 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    const char *pattern;
    const char *file_glob;
    const char *output_mode;
    bool search_files_only;
    int context;
    int offset;
    int limit;
    int count;
    int seen;
    bool truncated;
    size_t off;
    char *output;
    size_t output_size;
} search_ctx_t;

void tool_files_search_dir_recursive(const char *dir_path, search_ctx_t *ctx);
