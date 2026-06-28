#include "drivers/tool/tool_delegate_overview.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "autoconf.h"
#include "drivers/tool/tool_delegate_path_resolve.h"
#include "drivers/tool/tool_delegate_repo_batch.h"
#include "drivers/tool/tool_files.h"
#include "linux/kernel.h"
#include "linux/slab.h"
#include "paths.h"

static bool text_contains_any(const char *text, const char *const *keywords, size_t keyword_count)
{
    if (!text) {
        return false;
    }
    for (size_t i = 0; i < keyword_count; i++) {
        if (keywords[i] && strstr(text, keywords[i])) {
            return true;
        }
    }
    return false;
}

static bool text_has_any_keyword(const char *text, const char *const *keywords, size_t count)
{
    return text_contains_any(text, keywords, count);
}

static void append_clipped_text(char *dst, size_t dst_size, const char *src, size_t clip_chars)
{
    size_t dst_len;
    size_t src_len;
    bool clipped;
    size_t copy_len;
    size_t remain;

    if (!dst || dst_size == 0 || !src || !src[0]) {
        return;
    }

    dst_len = strlen(dst);
    if (dst_len >= dst_size - 1) {
        return;
    }

    src_len = strlen(src);
    clipped = src_len > clip_chars;
    copy_len = clipped ? clip_chars : src_len;
    remain = dst_size - 1 - dst_len;
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

static void compact_injected_file_excerpt(char *text, size_t text_size)
{
    char *hint;

    if (!text || !text[0] || text_size == 0) {
        return;
    }

    hint = strstr(text, "\n[Hint]");
    if (hint) {
        *hint = '\0';
    }
    if (strlen(text) > 2600) {
        char original[READ_FILE_MAX_CHARS + 1024];
        strscpy(original, text, sizeof(original));
        text[0] = '\0';
        append_clipped_text(text, text_size, original, 2600);
    }
}

static bool text_looks_like_deep_architecture_analysis(const char *prompt, const char *description)
{
    static const char *const deep_keywords[] = {
        "调用关系", "调用链",
        "主流程", "执行流程", "数据流", "状态流", "核心数据结构", "关键函数",
        "职责拆分", "模块职责", "依赖关系", "协作关系", "设计分析", "实现原理",
        "入口与主流程", "最后给我一份合并总结",
        "architecture", "architectural", "call flow", "control flow", "execution flow",
        "data flow", "state flow", "key data structures", "critical data structures",
        "key functions", "module responsibilities", "dependency graph", "how it works",
        "implementation details", "main flow", "merge summary"
    };

    return text_contains_any(prompt, deep_keywords, ARRAY_SIZE(deep_keywords)) ||
           text_contains_any(description, deep_keywords, ARRAY_SIZE(deep_keywords));
}

static bool text_requires_cross_module_role_analysis(const char *prompt, const char *description)
{
    static const char *const role_keywords[] = {
        "整体工作流", "整体流程", "全局流程", "主链路", "主链", "协作关系", "交互关系",
        "上下游", "在整体中的角色", "在整体工作流里的角色", "不要只看目录名",
        "how it fits", "overall workflow", "global flow", "main workflow",
        "role in the system", "role in the workflow", "interaction", "upstream", "downstream"
    };

    return text_contains_any(prompt, role_keywords, ARRAY_SIZE(role_keywords)) ||
           text_contains_any(description, role_keywords, ARRAY_SIZE(role_keywords));
}

static bool prompt_looks_like_repo_overview(const char *prompt, const char *description)
{
    static const char *const overview_keywords[] = {
        "目录结构", "完整结构", "项目结构", "仓库结构", "代码组织", "模块划分",
        "所有子目录", "所有目录", "目录树", "完整目录树",
        "关键文件", "关键模块", "代表性文件", "主要职责", "模块职责", "完整摸底",
        "入口文件", "入口",
        "directory structure", "repo structure", "project structure",
        "complete structure", "full structure", "all subdirectories",
        "all directories", "directory tree", "module map",
        "representative files", "key modules", "entry file", "entrypoint",
        "code organization"
    };

    return text_contains_any(prompt, overview_keywords, ARRAY_SIZE(overview_keywords)) ||
           text_contains_any(description, overview_keywords, ARRAY_SIZE(overview_keywords));
}

static void build_bounded_explore_prompt(const char *description,
                                         const char *prompt,
                                         const char *target_path,
                                         char *prepared_prompt,
                                         size_t prepared_prompt_size)
{
    char path[512];
    bool has_path = false;

    if (!prepared_prompt || prepared_prompt_size == 0) {
        return;
    }
    if (target_path && target_path[0]) {
        strscpy(path, target_path, sizeof(path));
        has_path = true;
    } else {
        has_path = tool_delegate_extract_single_absolute_repo_path(prompt, path, sizeof(path));
    }
    snprintf(prepared_prompt, prepared_prompt_size,
             "Scoped explore task:\n"
             "Target path: %s\n"
             "Task label: %s\n"
             "Original request: %s\n"
             "\n"
             "Do this:\n"
             "1. Start from the exact target path above, not from workspace or sibling roots.\n"
             "2. Collect only the minimum directory/file evidence needed to answer the original request.\n"
             "3. Explain responsibilities, key modules, and how this scope fits the larger workflow when relevant.\n"
             "\n"
             "Rules:\n"
             "- Do not derive extra target paths from task labels, session ids, coordinator ids, or repo nicknames.\n"
             "- Do not enumerate every subdirectory or every file.\n"
             "- Treat '完整结构' / '所有子目录' / '关键文件全量列表' as representative coverage, not exhaustive traversal.\n"
             "- Hard budget: at most 1 top-level listing, 2 focused follow-up listings/searches, and 2 targeted reads.\n"
             "- If you already identified the entrypoint family and representative module paths, stop and synthesize findings immediately.\n"
             "- Do not descend into more sibling directories just to get more examples once the role boundary is already clear.\n"
             "- Prefer reading README / package manifest / main entry file over repeatedly listing nested directories.\n"
             "- Ignore build artifacts like .o unless explicitly requested.\n"
             "- Stop once the answer is supported by concrete evidence.\n"
             "- Final answer must contain findings, not a narration of further exploration.\n",
             has_path ? path : "(unknown path)",
             description && description[0] ? description : "(no task label)",
             prompt && prompt[0] ? prompt : (description && description[0] ? description : ""));
}

static void append_repo_root_guidance(const char *prompt,
                                      const char *target_path,
                                      char *prepared_prompt,
                                      size_t prepared_prompt_size)
{
    char repo_root[512];

    if (!prepared_prompt || prepared_prompt_size == 0) {
        return;
    }
    if (target_path && target_path[0]) {
        strscpy(repo_root, target_path, sizeof(repo_root));
    } else if (!prompt ||
               !tool_delegate_extract_single_absolute_repo_path(prompt, repo_root, sizeof(repo_root)) ||
               !repo_root[0]) {
        return;
    }
    if (strlen(prepared_prompt) + strlen(repo_root) + 512 >= prepared_prompt_size) {
        return;
    }

    strlcat(prepared_prompt,
            "\n\nResolved repo root:\n- Treat this absolute path as the primary working scope for file exploration.\n- Prefer using this exact path, or children under it, in `files` tool calls instead of guessing `/repo`, `/project`, or unrelated relative paths.\n- If you need to orient first, list this path before exploring deeper.\n- Repo root: ",
            prepared_prompt_size);
    strlcat(prepared_prompt, repo_root, prepared_prompt_size);
}

static void append_workspace_contract_guidance(char *prepared_prompt,
                                               size_t prepared_prompt_size)
{
    const char *workspace;

    if (!prepared_prompt || prepared_prompt_size == 0) {
        return;
    }

    workspace = path_workspace_dir();
    if (!workspace || !workspace[0]) {
        return;
    }
    if (strlen(prepared_prompt) + strlen(workspace) + 640 >= prepared_prompt_size) {
        return;
    }

    strlcat(prepared_prompt,
            "\n\nWorkspace contract:\n- Agent workspace root: ",
            prepared_prompt_size);
    strlcat(prepared_prompt, workspace, prepared_prompt_size);
    strlcat(prepared_prompt,
            "\n"
            "- Never guess fake roots like `/repo`, `/workspace`, `/project`, or `/data/workspace`.\n"
            "- Prefer the injected absolute target path or resolved repo root for `files` calls.\n"
            "- When shell is necessary, set `terminal.workdir` to the exact project path instead of using `cd ... && ...` probing.\n",
            prepared_prompt_size);
}

static void append_authoritative_target_contract(const char *description,
                                                 const char *target_path,
                                                 char *prepared_prompt,
                                                 size_t prepared_prompt_size)
{
    if (!prepared_prompt || prepared_prompt_size == 0 || !target_path || !target_path[0]) {
        return;
    }
    if (strlen(prepared_prompt) + strlen(target_path) + strlen(description ? description : "") + 1200 >=
        prepared_prompt_size) {
        return;
    }

    strlcat(prepared_prompt,
            "\n\nAuthoritative target contract:\n"
            "- Primary target path: ",
            prepared_prompt_size);
    strlcat(prepared_prompt, target_path, prepared_prompt_size);
    strlcat(prepared_prompt,
            "\n"
            "- Requested task label: ",
            prepared_prompt_size);
    strlcat(prepared_prompt,
            description && description[0] ? description : "(no description)",
            prepared_prompt_size);
    strlcat(prepared_prompt,
            "\n"
            "- Treat the absolute target path above as authoritative. It is stronger than task labels, session ids, shorthand names, or inferred aliases.\n"
            "- Start from this exact path or its direct children. Do not begin from the workspace root unless the target path itself is the workspace root.\n"
            "- Do not search the workspace for task-label fragments such as `a1`, `b1`, `h1`, `l1`, `pa11`, `pb11`, `prr`, `block-ready`, or similar coordinator/test identifiers.\n"
            "- Do not derive paths from task ids, session ids, coordinator ids, or prompt nicknames. Use the injected absolute target path instead.\n"
            "- If the target path is a directory, first inspect that directory directly. If it is a file, inspect that file directly.\n"
            "- If the target path does not exist, report that mismatch explicitly instead of probing sibling fake paths.\n",
            prepared_prompt_size);
}

static bool extract_single_absolute_c_file_path(const char *prompt, char *path, size_t path_size)
{
    if (!prompt || !path || path_size == 0) {
        return false;
    }
    path[0] = '\0';

    const char *start = strstr(prompt, "/");
    while (start) {
        const char *end = start;
        while (*end && !isspace((unsigned char)*end) && *end != '"' && *end != '\'' &&
               *end != ',' && *end != ')' && *end != '(') {
            end++;
        }
        size_t len = (size_t)(end - start);
        if (len > 2 && len < path_size && strncmp(end - 2, ".c", 2) == 0) {
            memcpy(path, start, len);
            path[len] = '\0';
            return true;
        }
        start = strstr(end, "/");
    }
    return false;
}

static bool read_file_excerpt(const char *path, char *out, size_t out_size)
{
    char input_json[1024];
    char *buf;
    err_t err;

    if (!path || !path[0] || !out || out_size == 0) {
        return false;
    }

    snprintf(input_json, sizeof(input_json),
             "{\"path\":\"%s\",\"offset\":1,\"limit\":220}",
             path);
    buf = kzalloc(READ_FILE_MAX_CHARS + 1024, GFP_KERNEL);
    if (!buf) {
        return false;
    }
    err = tool_read_file_execute(input_json, buf, READ_FILE_MAX_CHARS + 1024);
    if (err != 0 || !buf[0]) {
        kfree(buf);
        return false;
    }
    strscpy(out, buf, out_size);
    compact_injected_file_excerpt(out, out_size);
    kfree(buf);
    return true;
}

bool tool_delegate_prepare_subagent_prompt(const char *subagent_type,
                                           const char *description,
                                           const char *target_path,
                                           const char *prompt,
                                           char *prepared_prompt,
                                           size_t prepared_prompt_size,
                                           bool *disable_tools)
{
    if (!prepared_prompt || prepared_prompt_size == 0) {
        return false;
    }
    prepared_prompt[0] = '\0';
    if (disable_tools) {
        *disable_tools = false;
    }

    strscpy(prepared_prompt, prompt ? prompt : "", prepared_prompt_size);
    if (!subagent_type || strcmp(subagent_type, "explore") != 0 || !prompt) {
        if (prompt &&
            (strcmp(subagent_type ? subagent_type : "", "librarian") == 0 ||
             strcmp(subagent_type ? subagent_type : "", "oracle") == 0)) {
            append_repo_root_guidance(prompt, target_path, prepared_prompt, prepared_prompt_size);
            append_workspace_contract_guidance(prepared_prompt, prepared_prompt_size);
            append_authoritative_target_contract(description, target_path, prepared_prompt, prepared_prompt_size);
        }
        return true;
    }

    if (prompt_looks_like_repo_overview(prompt, description)) {
        build_bounded_explore_prompt(description, prompt, target_path, prepared_prompt, prepared_prompt_size);
        append_repo_root_guidance(prompt, target_path, prepared_prompt, prepared_prompt_size);
        append_workspace_contract_guidance(prepared_prompt, prepared_prompt_size);
        append_authoritative_target_contract(description, target_path, prepared_prompt, prepared_prompt_size);
        return true;
    }

    append_repo_root_guidance(prompt, target_path, prepared_prompt, prepared_prompt_size);
    append_workspace_contract_guidance(prepared_prompt, prepared_prompt_size);
    append_authoritative_target_contract(description, target_path, prepared_prompt, prepared_prompt_size);

    char path[512];
    if (!extract_single_absolute_c_file_path(prompt, path, sizeof(path))) {
        return true;
    }

    char file_excerpt[READ_FILE_MAX_CHARS + 1024];
    if (!read_file_excerpt(path, file_excerpt, sizeof(file_excerpt))) {
        return true;
    }

    snprintf(prepared_prompt, prepared_prompt_size,
             "%s\n\nYou already have the target file content below. Do not call files/read_file/list first. "
             "Use only the provided content and return findings directly.\n\nProvided file content:\n%s",
             prompt,
             file_excerpt);
    if (disable_tools) {
        *disable_tools = true;
    }
    return true;
}

bool tool_delegate_request_is_bounded_explore_overview(const delegate_request_t *req)
{
    static const char *const broad_keywords[] = {
        "目录结构", "完整结构", "代码组织", "仓库结构", "项目结构",
        "所有子目录", "所有目录", "目录树", "完整目录树",
        "关键文件", "关键模块", "关键模块关系", "模块关系",
        "代表性文件", "主要职责", "模块职责", "职责边界", "协作边界", "完整摸底",
        "入口文件", "入口",
        "top-level structure", "directory structure", "repo structure",
        "complete structure", "full structure", "all subdirectories",
        "all directories", "directory tree", "module map",
        "representative files", "key modules", "module relationships",
        "module boundaries", "collaboration boundaries", "entry file", "entrypoint",
        "code organization"
    };
    bool has_overview_signal;

    if (!req) {
        return false;
    }
    if (strcmp(req->subagent_type, "explore") == 0 &&
        req->target_path[0] &&
        tool_delegate_file_is_directory(req->target_path)) {
        return true;
    }

    has_overview_signal =
        text_has_any_keyword(req->prompt, broad_keywords, ARRAY_SIZE(broad_keywords)) ||
        text_has_any_keyword(req->description, broad_keywords, ARRAY_SIZE(broad_keywords));
    if (!has_overview_signal) {
        return false;
    }
    if (text_looks_like_deep_architecture_analysis(req->prompt, req->description) &&
        !tool_delegate_overview_request_preserves_repo_root(req->prompt, req->description)) {
        return false;
    }
    return true;
}

bool tool_delegate_request_requires_deeper_explore_analysis(const delegate_request_t *req)
{
    if (!req) {
        return false;
    }
    if (strcmp(req->subagent_type, "explore") != 0) {
        return false;
    }

    if (text_looks_like_deep_architecture_analysis(req->prompt, req->description)) {
        return true;
    }
    if (text_requires_cross_module_role_analysis(req->prompt, req->description)) {
        return true;
    }
    return false;
}

bool tool_delegate_overview_request_preserves_repo_root(const char *prompt, const char *description)
{
    static const char *const root_preserve_keywords[] = {
        "顶层目录", "顶层结构", "代码库结构", "仓库结构", "项目结构", "各目录职责",
        "整体结构", "整仓", "整个仓库", "全仓", "全局结构",
        "top-level", "top level", "codebase structure", "repo structure",
        "project structure", "directory responsibilities", "whole repository",
        "entire repository", "overall structure"
    };

    return text_contains_any(prompt, root_preserve_keywords, ARRAY_SIZE(root_preserve_keywords)) ||
           text_contains_any(description, root_preserve_keywords, ARRAY_SIZE(root_preserve_keywords));
}
