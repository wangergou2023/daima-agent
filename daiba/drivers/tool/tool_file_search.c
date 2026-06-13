/* 文件搜索辅助层：递归搜索文件名或文本内容。 */

#include "drivers/tool/tool_file_search.h"

#include "drivers/tool/tool_file_ops.h"
#include "core/config.h"

#include <dirent.h>
#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define TOOL_SEARCH_PATH_SIZE 1024

static bool is_skipped_dir_name(const char *name)
{
    return strcmp(name, ".") == 0 ||
           strcmp(name, "..") == 0 ||
           strcmp(name, ".git") == 0 ||
           strcmp(name, ".svn") == 0 ||
           strcmp(name, "build") == 0 ||
           strcmp(name, "build-host") == 0 ||
           strcmp(name, "node_modules") == 0 ||
           strcmp(name, ".venv") == 0 ||
           strcmp(name, "venv") == 0;
}

static bool file_name_matches(const char *name, const char *pattern)
{
    if (!name || !pattern || !pattern[0]) {
        return false;
    }
    if (strchr(pattern, '*') || strchr(pattern, '?')) {
        return fnmatch(pattern, name, 0) == 0;
    }
    return strstr(name, pattern) != NULL;
}

static bool file_glob_matches(const char *name, const char *file_glob)
{
    if (!file_glob || !file_glob[0]) {
        return true;
    }
    return fnmatch(file_glob, name, 0) == 0;
}

static bool file_looks_binary(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return true;
    }

    unsigned char buf[512];
    size_t n = fread(buf, 1, sizeof(buf), f);
    fclose(f);

    if (n == 0) {
        return false;
    }

    for (size_t i = 0; i < n; i++) {
        if (buf[i] == '\0') {
            return true;
        }
    }
    return false;
}

static bool append_search_line(search_ctx_t *ctx, const char *line)
{
    if (!ctx || !line || !line[0]) {
        return false;
    }

    size_t need = strlen(line);
    if (ctx->off + need >= ctx->output_size - 1) {
        ctx->truncated = true;
        return false;
    }

    memcpy(ctx->output + ctx->off, line, need);
    ctx->off += need;
    ctx->output[ctx->off] = '\0';
    return true;
}

static bool search_mode_files_only(const search_ctx_t *ctx)
{
    return ctx && ctx->output_mode && strcmp(ctx->output_mode, "files_only") == 0;
}

static bool search_mode_count(const search_ctx_t *ctx)
{
    return ctx && ctx->output_mode && strcmp(ctx->output_mode, "count") == 0;
}

static bool emit_search_content_line(search_ctx_t *ctx,
                                     const char *path,
                                     int line_no,
                                     const char *line)
{
    if (!ctx || !path || !line) {
        return false;
    }

    char trimmed[DAIMA_SEARCH_FILES_MAX_LINE_CHARS + 32];
    if (strlen(line) > DAIMA_SEARCH_FILES_MAX_LINE_CHARS) {
        snprintf(trimmed, sizeof(trimmed), "%.*s... [truncated]",
                 DAIMA_SEARCH_FILES_MAX_LINE_CHARS, line);
    } else {
        snprintf(trimmed, sizeof(trimmed), "%s", line);
    }

    char row[TOOL_SEARCH_PATH_SIZE + DAIMA_SEARCH_FILES_MAX_LINE_CHARS + 64];
    snprintf(row, sizeof(row), "%s:%d: %s\n", path, line_no, trimmed);
    return append_search_line(ctx, row);
}

static void search_file_content(const char *path, search_ctx_t *ctx)
{
    if (!path || !ctx || ctx->count >= ctx->limit || file_looks_binary(path)) {
        return;
    }

    FILE *f = fopen(path, "r");
    if (!f) {
        return;
    }

    char *prev_lines[DAIMA_SEARCH_FILES_MAX_CONTEXT] = {0};
    int prev_nums[DAIMA_SEARCH_FILES_MAX_CONTEXT] = {0};
    int prev_count = 0;
    char *line = NULL;
    size_t cap = 0;
    int line_no = 0;
    int file_match_count = 0;
    int post_remaining = 0;
    int last_emitted_line = 0;

    while (getline(&line, &cap, f) != -1) {
        if (ctx->count >= ctx->limit) {
            ctx->truncated = true;
            break;
        }
        line_no++;
        bool matched = strstr(line, ctx->pattern) != NULL;

        if (matched) {
            file_match_count++;
        }

        if (matched && search_mode_files_only(ctx)) {
            ctx->seen++;
            if (ctx->seen > ctx->offset) {
                char row[TOOL_SEARCH_PATH_SIZE + 8];
                snprintf(row, sizeof(row), "%s\n", path);
                if (append_search_line(ctx, row)) {
                    ctx->count++;
                }
            }
            break;
        }

        if (matched && !search_mode_count(ctx)) {
            ctx->seen++;
            if (ctx->seen <= ctx->offset) {
                matched = false;
            } else {
                for (int i = 0; i < prev_count; i++) {
                    if (prev_nums[i] <= last_emitted_line) {
                        continue;
                    }
                    if (!emit_search_content_line(ctx, path, prev_nums[i], prev_lines[i])) {
                        goto search_cleanup;
                    }
                    last_emitted_line = prev_nums[i];
                }

                tool_files_trim_line_end(line);
                if (line_no > last_emitted_line) {
                    if (!emit_search_content_line(ctx, path, line_no, line)) {
                        goto search_cleanup;
                    }
                    last_emitted_line = line_no;
                }
                ctx->count++;
                post_remaining = ctx->context;
            }
        } else if (!matched && post_remaining > 0 && !search_mode_count(ctx)) {
            tool_files_trim_line_end(line);
            if (line_no > last_emitted_line) {
                if (!emit_search_content_line(ctx, path, line_no, line)) {
                    goto search_cleanup;
                }
                last_emitted_line = line_no;
            }
            post_remaining--;
        }

        if (ctx->context > 0) {
            tool_files_trim_line_end(line);
            if (prev_count < ctx->context) {
                prev_lines[prev_count] = strdup(line);
                prev_nums[prev_count] = line_no;
                if (prev_lines[prev_count]) {
                    prev_count++;
                }
            } else {
                free(prev_lines[0]);
                for (int i = 1; i < prev_count; i++) {
                    prev_lines[i - 1] = prev_lines[i];
                    prev_nums[i - 1] = prev_nums[i];
                }
                prev_lines[prev_count - 1] = strdup(line);
                prev_nums[prev_count - 1] = line_no;
            }
        }
    }

    if (search_mode_count(ctx) && file_match_count > 0 && ctx->count < ctx->limit) {
        ctx->seen++;
        if (ctx->seen > ctx->offset) {
            char row[TOOL_SEARCH_PATH_SIZE + 32];
            snprintf(row, sizeof(row), "%s: %d\n", path, file_match_count);
            if (append_search_line(ctx, row)) {
                ctx->count++;
            }
        }
    }

search_cleanup:
    for (int i = 0; i < prev_count; i++) {
        free(prev_lines[i]);
    }
    free(line);
    fclose(f);
}

void tool_files_search_dir_recursive(const char *dir_path, search_ctx_t *ctx)
{
    if (!dir_path || !ctx || ctx->count >= ctx->limit) {
        return;
    }

    DIR *dir = opendir(dir_path);
    if (!dir) {
        return;
    }

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (ctx->count >= ctx->limit) {
            ctx->truncated = true;
            break;
        }
        if (is_skipped_dir_name(ent->d_name)) {
            continue;
        }

        char full_path[TOOL_SEARCH_PATH_SIZE];
        if (snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, ent->d_name) >= (int)sizeof(full_path)) {
            continue;
        }

        struct stat st;
        if (stat(full_path, &st) != 0) {
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            tool_files_search_dir_recursive(full_path, ctx);
            continue;
        }
        if (!S_ISREG(st.st_mode) || !file_glob_matches(ent->d_name, ctx->file_glob)) {
            continue;
        }

        if (ctx->search_files_only) {
            if (!file_name_matches(ent->d_name, ctx->pattern) &&
                !file_name_matches(full_path, ctx->pattern)) {
                continue;
            }
            ctx->seen++;
            if (ctx->seen <= ctx->offset) {
                continue;
            }
            char row[TOOL_SEARCH_PATH_SIZE + 32];
            if (search_mode_count(ctx)) {
                snprintf(row, sizeof(row), "%s: 1\n", full_path);
            } else {
                snprintf(row, sizeof(row), "%s\n", full_path);
            }
            if (!append_search_line(ctx, row)) {
                break;
            }
            ctx->count++;
        } else {
            search_file_content(full_path, ctx);
        }
    }

    closedir(dir);
}
