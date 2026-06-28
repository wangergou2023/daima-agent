#include "drivers/tool/tool_delegate_repo_batch.h"

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "cjson.h"
#include "drivers/tool/tool_delegate_path_resolve.h"
#include "linux/kernel.h"
#include "linux/printk.h"
#include "lib/text.h"

static bool path_already_selected(char paths[][512], int count, const char *candidate)
{
    if (!paths || !candidate || !candidate[0]) {
        return false;
    }
    for (int i = 0; i < count; i++) {
        if (strcmp(paths[i], candidate) == 0) {
            return true;
        }
    }
    return false;
}

static bool prompt_has_multiple_repo_relative_scope_mentions(const char *text,
                                                             const char *repo_root)
{
    const char *repo_name;
    char needle[160];
    const char *cursor;
    int hits = 0;

    if (!text || !text[0] || !repo_root || !repo_root[0]) {
        return false;
    }

    repo_name = tool_delegate_path_basename(repo_root);
    if (!repo_name || !repo_name[0] ||
        snprintf(needle, sizeof(needle), "%s/", repo_name) >= (int)sizeof(needle)) {
        return false;
    }

    cursor = text;
    while ((cursor = strstr(cursor, needle)) != NULL) {
        hits++;
        if (hits >= 2) {
            return true;
        }
        cursor += strlen(needle);
    }

    return false;
}

static bool collect_explicit_prompt_paths(const delegate_request_t *req,
                                          char paths[][512],
                                          int *out_count)
{
    const char *text = NULL;
    char repo_root[512];
    const char *cursor = NULL;
    int count = 0;

    if (!req || !paths || !out_count) {
        return false;
    }
    *out_count = 0;
    text = req->prompt[0] ? req->prompt : req->description;
    if (!text || !text[0] ||
        !tool_delegate_resolve_repo_root(req, repo_root, sizeof(repo_root)) ||
        !repo_root[0]) {
        return false;
    }

    strscpy(paths[count++], repo_root, sizeof(paths[0]));
    cursor = text;
    while (cursor && *cursor && count < 4) {
        char normalized[512];
        const char *repo_hit = strstr(cursor, tool_delegate_path_basename(repo_root));

        if (!repo_hit) {
            break;
        }
        if (!tool_delegate_extract_repo_scoped_path(repo_hit, repo_root, normalized, sizeof(normalized)) ||
            !normalized[0] ||
            strcmp(normalized, repo_root) == 0 ||
            strncmp(normalized, repo_root, strlen(repo_root)) != 0 ||
            path_already_selected(paths, count, normalized)) {
            cursor = repo_hit + strlen(tool_delegate_path_basename(repo_root));
            continue;
        }

        strscpy(paths[count++], normalized, sizeof(paths[0]));
        cursor = repo_hit + strlen(tool_delegate_path_basename(repo_root));
    }

    if (count < 2 && prompt_has_multiple_repo_relative_scope_mentions(text, repo_root)) {
        return true;
    }
    *out_count = count;
    return count >= 2;
}

static bool resolve_repo_root_auto_batch_scopes(const delegate_request_t *req,
                                                char paths[][512],
                                                int *out_count)
{
    char repo_root[512];
    int count = 0;
    static const char *preferred_children[] = {
        "kernel",
        "drivers/tool",
        "drivers/llm",
        "src",
        "app",
        "cmd",
        "internal",
        "lib",
        "pkg",
        "packages",
        "services",
        "modules",
        "docs",
    };

    if (!req || !paths || !out_count) {
        return false;
    }
    *out_count = 0;

    if (!tool_delegate_resolve_repo_root(req, repo_root, sizeof(repo_root))) {
        return false;
    }
    if (!tool_delegate_file_is_directory(repo_root)) {
        return false;
    }

    strscpy(paths[count++], repo_root, sizeof(paths[0]));

    for (size_t i = 0;
         i < sizeof(preferred_children) / sizeof(preferred_children[0]) && count < 4;
         i++) {
        char candidate[512];
        snprintf(candidate, sizeof(candidate), "%s/%s", repo_root, preferred_children[i]);
        if (access(candidate, F_OK) == 0 && !path_already_selected(paths, count, candidate)) {
            strscpy(paths[count++], candidate, sizeof(paths[0]));
        }
    }

    *out_count = count;
    return count >= 3;
}

static bool resolve_batch_scopes(const delegate_request_t *req,
                                 char paths[][512],
                                 int *out_count)
{
    if (collect_explicit_prompt_paths(req, paths, out_count)) {
        return true;
    }
    return resolve_repo_root_auto_batch_scopes(req, paths, out_count);
}

bool tool_delegate_should_expand_repo_root_overview_batch(const delegate_request_t *req)
{
    char paths[4][512];
    int count = 0;
    bool is_batch_request;
    bool is_background_request;
    bool has_target_path;
    bool target_path_blocks_batch;
    bool is_explore;
    bool bounded_overview;
    bool preserves_root;
    bool resolved_paths;
    bool has_explicit_scopes;

    if (!req) {
        return false;
    }
    is_batch_request = req->is_batch;
    is_background_request = req->run_in_background;
    has_target_path = req->target_path[0] != '\0';
    target_path_blocks_batch = false;
    is_explore = strcmp(req->subagent_type, "explore") == 0;
    has_explicit_scopes = collect_explicit_prompt_paths(req, paths, &count);
    bounded_overview = tool_delegate_request_is_bounded_explore_overview(req) || has_explicit_scopes;
    preserves_root = tool_delegate_overview_request_preserves_repo_root(req->prompt, req->description);

    if (has_target_path) {
        target_path_blocks_batch = !resolve_batch_scopes(req, paths, &count);
    }

    if (is_batch_request || is_background_request || target_path_blocks_batch) {
        pr_info("delegate repo-root batch skip: desc=%s reason=batch=%d background=%d target_path=%d target_path_blocks=%d",
                req->description[0] ? req->description : "-",
                is_batch_request,
                is_background_request,
                has_target_path,
                target_path_blocks_batch);
        return false;
    }
    if (!is_explore) {
        pr_info("delegate repo-root batch skip: desc=%s reason=subagent_type=%s",
                req->description[0] ? req->description : "-",
                req->subagent_type[0] ? req->subagent_type : "-");
        return false;
    }
    if (!bounded_overview) {
        pr_info("delegate repo-root batch skip: desc=%s reason=not_bounded_overview",
                req->description[0] ? req->description : "-");
        return false;
    }
    if (!preserves_root && !has_explicit_scopes) {
        pr_info("delegate repo-root batch skip: desc=%s reason=root_not_preserved",
                req->description[0] ? req->description : "-");
        return false;
    }

    resolved_paths = has_target_path ? !target_path_blocks_batch
                                     : resolve_batch_scopes(req, paths, &count);
    if (!resolved_paths) {
        pr_info("delegate repo-root batch skip: desc=%s reason=scope_resolution_failed",
                req->description[0] ? req->description : "-");
        return false;
    }
    return true;
}

void tool_delegate_fill_repo_root_overview_batch_request(const delegate_request_t *req,
                                                         delegate_request_t *batch_req)
{
    char paths[4][512];
    int count = 0;

    if (!req || !batch_req) {
        return;
    }

    memset(batch_req, 0, sizeof(*batch_req));
    if (!resolve_batch_scopes(req, paths, &count)) {
        return;
    }

    batch_req->is_batch = true;
    batch_req->batch_count = 0;

    for (int i = 1; i < count && batch_req->batch_count < DELEGATE_COORDINATOR_AGENTS_MAX; i++) {
        const char *path = paths[i];
        const char *leaf = tool_delegate_path_basename(path);
        int idx = batch_req->batch_count++;
        snprintf(batch_req->batch_tasks[idx].description,
                 sizeof(batch_req->batch_tasks[idx].description),
                 "分析 %s 目录结构",
                 leaf && leaf[0] ? leaf : path);
        snprintf(batch_req->batch_tasks[idx].prompt,
                 sizeof(batch_req->batch_tasks[idx].prompt),
                 "分析 %s 的目录结构和关键模块。"
                 "只做代表性覆盖，不要穷举。"
                 "总结直接子目录、核心文件、主要职责，以及它与整仓主链的关系。",
                 path);
        strscpy(batch_req->batch_tasks[idx].subagent_type, "explore",
                sizeof(batch_req->batch_tasks[idx].subagent_type));
        strscpy(batch_req->batch_tasks[idx].target_path,
                path,
                sizeof(batch_req->batch_tasks[idx].target_path));
    }
}

char *tool_delegate_build_repo_root_overview_batch_json(const delegate_request_t *req,
                                                        bool include_sudo_preflight)
{
    delegate_request_t batch_req;
    cJSON *root = NULL;
    cJSON *tasks = NULL;
    char *json = NULL;
    char repo_root[512];

    if (!req) {
        return NULL;
    }
    memset(&batch_req, 0, sizeof(batch_req));
    memset(repo_root, 0, sizeof(repo_root));
    tool_delegate_fill_repo_root_overview_batch_request(req, &batch_req);
    if (!batch_req.is_batch || batch_req.batch_count <= 0) {
        return NULL;
    }
    if (!tool_delegate_resolve_repo_root(req, repo_root, sizeof(repo_root)) || !repo_root[0]) {
        return NULL;
    }

    root = cJSON_CreateObject();
    tasks = cJSON_CreateArray();
    if (!root || !tasks) {
        cJSON_Delete(root);
        cJSON_Delete(tasks);
        return NULL;
    }

    for (int i = 0; i < batch_req.batch_count; i++) {
        cJSON *task = cJSON_CreateObject();
        if (!task) {
            continue;
        }
        cJSON_AddStringToObject(task, "description", batch_req.batch_tasks[i].description);
        cJSON_AddStringToObject(task, "subagent_type", batch_req.batch_tasks[i].subagent_type);
        cJSON_AddStringToObject(task, "target_path", batch_req.batch_tasks[i].target_path);
        cJSON_AddStringToObject(task, "prompt", batch_req.batch_tasks[i].prompt);
        cJSON_AddItemToArray(tasks, task);
    }

    if (include_sudo_preflight) {
        cJSON *task = cJSON_CreateObject();
        cJSON *preflight = cJSON_CreateObject();
        cJSON *input = cJSON_CreateObject();

        if (task && preflight && input) {
            cJSON_AddStringToObject(task, "description", "验证 sudo 权限链路");
            cJSON_AddStringToObject(task, "subagent_type", "explore");
            cJSON_AddStringToObject(task, "target_path", repo_root);
            cJSON_AddStringToObject(task, "prompt",
                                    "验证 sudo 权限链路，并基于真实工具结果解释为什么会请求 sudo、如果用户取消会如何阻塞。不要假装执行，必须基于 preflight_tool 的真实输出总结。");
            cJSON_AddStringToObject(preflight, "tool_name", "terminal");
            cJSON_AddStringToObject(input, "command", "sudo ls /root");
            cJSON_AddStringToObject(input, "workdir", repo_root);
            cJSON_AddItemToObject(preflight, "input", input);
            cJSON_AddBoolToObject(preflight, "continue_on_error", false);
            cJSON_AddItemToObject(task, "preflight_tool", preflight);
            cJSON_AddItemToArray(tasks, task);
        } else {
            cJSON_Delete(task);
            cJSON_Delete(preflight);
            cJSON_Delete(input);
        }
    }

    cJSON_AddItemToObject(root, "tasks", tasks);
    json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}
