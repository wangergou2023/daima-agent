#include "drivers/tool/tool_delegate_local_overview.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "autoconf.h"
#include "drivers/tool/tool_delegate_path_resolve.h"
#include "drivers/tool/tool_delegate_repo_batch.h"
#include "drivers/tool/tool_files.h"
#include "linux/kernel.h"
#include "linux/slab.h"

static bool basename_has_suffix(const char *name, const char *suffix)
{
    size_t name_len;
    size_t suffix_len;

    if (!name || !suffix) {
        return false;
    }
    name_len = strlen(name);
    suffix_len = strlen(suffix);
    return name_len >= suffix_len &&
           strcmp(name + name_len - suffix_len, suffix) == 0;
}

static bool path_ends_with(const char *path, const char *suffix)
{
    size_t path_len;
    size_t suffix_len;

    if (!path || !suffix) {
        return false;
    }
    path_len = strlen(path);
    suffix_len = strlen(suffix);
    return path_len >= suffix_len &&
           strcmp(path + path_len - suffix_len, suffix) == 0;
}

static bool append_unique_line(char lines[][96], int *count, int max_count, const char *value)
{
    if (!lines || !count || !value || !value[0] || *count >= max_count) {
        return false;
    }
    for (int i = 0; i < *count; i++) {
        if (strcmp(lines[i], value) == 0) {
            return false;
        }
    }
    strscpy(lines[*count], value, 96);
    (*count)++;
    return true;
}

static bool append_unique_path(char lines[][160], int *count, int max_count, const char *value)
{
    if (!lines || !count || !value || !value[0] || *count >= max_count) {
        return false;
    }
    for (int i = 0; i < *count; i++) {
        if (strcmp(lines[i], value) == 0) {
            return false;
        }
    }
    strscpy(lines[*count], value, 160);
    (*count)++;
    return true;
}

static bool overview_should_skip_name(const char *name)
{
    static const char *const exact_skip[] = {
        ".", "..", ".git", ".gitignore", ".clang-format", ".config",
        "COPYING", "CREDITS", "REPORTING-BUGS"
    };
    static const char *const suffix_skip[] = {
        ".o", ".a", ".so", ".out", ".swp", ".tmp", ".log"
    };

    if (!name || !name[0]) {
        return true;
    }
    for (size_t i = 0; i < ARRAY_SIZE(exact_skip); i++) {
        if (strcmp(name, exact_skip[i]) == 0) {
            return true;
        }
    }
    for (size_t i = 0; i < ARRAY_SIZE(suffix_skip); i++) {
        if (basename_has_suffix(name, suffix_skip[i])) {
            return true;
        }
    }
    if (strncmp(name, "build", 5) == 0) {
        return true;
    }
    return false;
}

static bool overview_is_preferred_file(const char *name)
{
    if (!name || !name[0]) {
        return false;
    }
    return basename_has_suffix(name, ".c") ||
           basename_has_suffix(name, ".h") ||
           basename_has_suffix(name, ".md") ||
           basename_has_suffix(name, ".json") ||
           basename_has_suffix(name, ".txt") ||
           strcmp(name, "Makefile") == 0 ||
           strcmp(name, "AGENTS.md") == 0 ||
           strcmp(name, "README.md") == 0;
}

static void append_unique_abs_path(char items[][160], int *count, int max_count, const char *value)
{
    if (!items || !count || !value || !value[0] || *count >= max_count) {
        return;
    }
    for (int i = 0; i < *count; i++) {
        if (strcmp(items[i], value) == 0) {
            return;
        }
    }
    strscpy(items[*count], value, 160);
    (*count)++;
}

static bool collect_list_dir_paths(const char *dir_path,
                                   char items[][160],
                                   int max_items,
                                   int *out_count)
{
    char input_json[1200];
    char output[4096];
    char *cursor;
    int count = 0;

    if (!dir_path || !dir_path[0] || !items || max_items <= 0 || !out_count) {
        return false;
    }

    snprintf(input_json, sizeof(input_json),
             "{\"action\":\"list\",\"path\":\"%s\"}",
             dir_path);
    if (tool_list_dir_execute(input_json, output, sizeof(output)) != 0) {
        return false;
    }

    cursor = output;
    while (cursor && *cursor && count < max_items) {
        char *line_end = strchr(cursor, '\n');
        size_t len = line_end ? (size_t)(line_end - cursor) : strlen(cursor);
        if (len > 0 && len < 160 && cursor[0] == '/') {
            char line[160];
            memcpy(line, cursor, len);
            line[len] = '\0';
            append_unique_abs_path(items, &count, max_items, line);
        }
        cursor = line_end ? line_end + 1 : NULL;
    }

    *out_count = count;
    return count > 0;
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

static bool listing_has_suffix(char items[][160], int count, const char *suffix)
{
    for (int i = 0; i < count; i++) {
        if (path_ends_with(items[i], suffix)) {
            return true;
        }
    }
    return false;
}

static bool scope_hint_is_explicitly_requested(const char *text, const char *hint)
{
    static const char *const prefixes[] = {
        "分析 ", "看看 ", "查看 ", "聚焦 ", "只看 ", "扫描 ",
        "analyze ", "inspect ", "focus on ", "look at ", "scan "
    };
    char needle[256];
    const char *hit;

    if (!text || !hint || !hint[0]) {
        return false;
    }

    hit = strstr(text, hint);
    if (hit == NULL) {
        return false;
    }

    for (size_t i = 0; i < ARRAY_SIZE(prefixes); i++) {
        snprintf(needle, sizeof(needle), "%s%s", prefixes[i], hint);
        if (strstr(text, needle) != NULL) {
            return true;
        }
    }

    if (strchr(hint, '/')) {
        snprintf(needle, sizeof(needle), "/%s", hint);
        if (strstr(text, needle) != NULL) {
            return true;
        }
    }

    if (hit > text) {
        unsigned char prev = (unsigned char)hit[-1];
        const char *after = hit + strlen(hint);
        unsigned char next = (unsigned char)after[0];
        if (!isalnum(prev) && prev != '_' && prev != '-' &&
            (next == '\0' || isspace(next) ||
             next == ',' || next == ':' || next == ';' || next == ')' ||
             next == '.' || next == '!' || next == '?')) {
            return true;
        }
    } else {
        return true;
    }

    return false;
}

static bool maybe_refine_repo_root_from_scope_hint(const char *description,
                                                   const char *prompt,
                                                   char *repo_root,
                                                   size_t repo_root_size)
{
    static const char *const scoped_hints[] = {
        "drivers/tool",
        "drivers/llm",
        "kernel/turn",
        "kernel/context",
        "kernel/tooling",
        "kernel/channel",
        "kernel/runtime",
        "kernel/time",
        "kernel",
        "drivers",
        "docs",
        "arch",
    };
    char candidate[512];

    if (!repo_root || !repo_root[0]) {
        return false;
    }
    if (tool_delegate_overview_request_preserves_repo_root(prompt, description)) {
        return true;
    }

    for (size_t i = 0; i < ARRAY_SIZE(scoped_hints); i++) {
        const char *hint = scoped_hints[i];
        bool explicit_in_prompt = scope_hint_is_explicitly_requested(prompt, hint);
        bool explicit_in_description = scope_hint_is_explicitly_requested(description, hint);

        if (!explicit_in_prompt && !explicit_in_description) {
            continue;
        }
        if (path_ends_with(repo_root, hint)) {
            return true;
        }
        snprintf(candidate, sizeof(candidate), "%s/%s", repo_root, hint);
        if (access(candidate, F_OK) == 0) {
            strscpy(repo_root, candidate, repo_root_size);
            pr_info("delegate_repo_overview refined scope=%s", repo_root);
            return true;
        }
    }

    for (size_t i = 0; i < ARRAY_SIZE(scoped_hints); i++) {
        const char *hint = scoped_hints[i];
        const char *base = tool_delegate_path_basename(hint);
        bool explicit_in_prompt = scope_hint_is_explicitly_requested(prompt, base);
        bool explicit_in_description = scope_hint_is_explicitly_requested(description, base);

        if (!explicit_in_prompt && !explicit_in_description) {
            continue;
        }
        if (path_ends_with(repo_root, hint)) {
            return true;
        }
        snprintf(candidate, sizeof(candidate), "%s/%s", repo_root, hint);
        if (access(candidate, F_OK) == 0) {
            strscpy(repo_root, candidate, repo_root_size);
            pr_info("delegate_repo_overview refined scope=%s (basename match)", repo_root);
            return true;
        }
    }

    return true;
}

bool tool_delegate_try_local_repo_overview(const delegate_request_t *req,
                                           char *summary,
                                           size_t summary_size)
{
    char repo_root[512];
    char items[32][160];
    char dir_names[16][96];
    char file_names[16][96];
    char next_files[8][160];
    int item_count = 0;
    int dir_count = 0;
    int file_count = 0;
    int next_file_count = 0;

    if (!req || !summary || summary_size == 0) {
        return false;
    }
    summary[0] = '\0';

    if (!tool_delegate_request_is_bounded_explore_overview(req)) {
        return false;
    }
    if (!tool_delegate_resolve_repo_root(req, repo_root, sizeof(repo_root))) {
        return false;
    }
    if (!req->target_path[0]) {
        maybe_refine_repo_root_from_scope_hint(req->description, req->prompt, repo_root, sizeof(repo_root));
    }
    if (!collect_list_dir_paths(repo_root, items, 32, &item_count)) {
        return false;
    }

    for (int i = 0; i < item_count; i++) {
        const char *base = tool_delegate_path_basename(items[i]);
        if (!base[0] || overview_should_skip_name(base)) {
            continue;
        }
        if (tool_delegate_file_is_directory(items[i])) {
            append_unique_line(dir_names, &dir_count, 16, base);
        } else if (overview_is_preferred_file(base)) {
            append_unique_line(file_names, &file_count, 16, base);
        }
        if (next_file_count < 8 &&
            (basename_has_suffix(base, ".c") ||
             basename_has_suffix(base, ".h") ||
             basename_has_suffix(base, ".md") ||
             strcmp(base, "Makefile") == 0 ||
             strcmp(base, "AGENTS.md") == 0 ||
             strcmp(base, "README.md") == 0)) {
            append_unique_path(next_files, &next_file_count, 8, items[i]);
        }
    }

    strlcat(summary, "目录结构已经足够清楚，可以直接按“立即子目录 + 少量核心文件”建立职责边界，不需要做全量遍历。", summary_size);
    if (dir_count > 0) {
        strlcat(summary, "\n\n立即子目录：", summary_size);
        append_joined_items(summary, summary_size, dir_names, dir_count, "、");
    }
    if (file_count > 0) {
        strlcat(summary, "\n核心文件：", summary_size);
        append_joined_items(summary, summary_size, file_names, file_count, "、");
    }

    if (listing_has_suffix(items, item_count, "/turn") ||
        listing_has_suffix(items, item_count, "/turn_run.c") ||
        listing_has_suffix(items, item_count, "/turn_exec.c")) {
        strlcat(summary,
                "\n\n职责判断：这里承载回合执行主链，后续阅读应优先围绕 tool-call 循环、执行调度和回合恢复展开。",
                summary_size);
    } else if (listing_has_suffix(items, item_count, "/tool_delegate.c") ||
               listing_has_suffix(items, item_count, "/tool_runtime.c") ||
               listing_has_suffix(items, item_count, "/tool_invocation_context.c")) {
        strlcat(summary,
                "\n\n职责判断：这里属于工具编排与调用治理层，后续阅读应优先确认工具协议、delegate_task 协调、运行时封装和调用上下文修正。",
                summary_size);
    } else if (listing_has_suffix(items, item_count, "/model_fallback.c") ||
               listing_has_suffix(items, item_count, "/llm_proxy.c") ||
               listing_has_suffix(items, item_count, "/llm_openai_payload.c")) {
        strlcat(summary,
                "\n\n职责判断：这里是模型接入层，后续阅读应优先确认 provider 选择、payload 适配、fallback 和 response_format / tool 协议兼容。",
                summary_size);
    } else {
        strlcat(summary,
                "\n\n职责判断：这个区域已经拆成多个小模块，后续阅读应顺着目录边界确认职责，再挑 2-4 个代表性文件核对入口和依赖关系。",
                summary_size);
    }

    if (next_file_count > 0) {
        strlcat(summary, "\n\n建议继续看：", summary_size);
        append_joined_paths(summary, summary_size, next_files, next_file_count, "、");
    }

    strlcat(summary,
            "\n\n结论性质：以上判断基于当前目录枚举和代表性文件名，已经足够支持父代理分工与下一步定向深挖，但还不是完整静态审计。",
            summary_size);
    return true;
}
