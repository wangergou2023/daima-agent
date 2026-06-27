#include "drivers/tool/tool_delegate_repo_batch.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "linux/kernel.h"
#include "linux/printk.h"

static bool resolve_repo_root_auto_batch_scopes(const delegate_request_t *req,
                                                char paths[][512],
                                                int *out_count)
{
    char repo_root[512];
    int count = 0;

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

    if (access("/home/wangergou/code/github/daima-agent/kernel", F_OK) == 0 &&
        strcmp(repo_root, "/home/wangergou/code/github/daima-agent") == 0) {
        strscpy(paths[count++], "/home/wangergou/code/github/daima-agent/kernel", sizeof(paths[0]));
    } else {
        char candidate[512];
        snprintf(candidate, sizeof(candidate), "%s/kernel", repo_root);
        if (access(candidate, F_OK) == 0) {
            strscpy(paths[count++], candidate, sizeof(paths[0]));
        }
    }

    {
        char candidate[512];
        snprintf(candidate, sizeof(candidate), "%s/drivers/tool", repo_root);
        if (access(candidate, F_OK) == 0) {
            strscpy(paths[count++], candidate, sizeof(paths[0]));
        }
        snprintf(candidate, sizeof(candidate), "%s/drivers/llm", repo_root);
        if (access(candidate, F_OK) == 0) {
            strscpy(paths[count++], candidate, sizeof(paths[0]));
        }
    }

    *out_count = count;
    return count >= 3;
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

    if (!req) {
        return false;
    }
    is_batch_request = req->is_batch;
    is_background_request = req->run_in_background;
    has_target_path = req->target_path[0] != '\0';
    target_path_blocks_batch = false;
    is_explore = strcmp(req->subagent_type, "explore") == 0;
    bounded_overview = tool_delegate_request_is_bounded_explore_overview(req);
    preserves_root = tool_delegate_overview_request_preserves_repo_root(req->prompt, req->description);

    if (has_target_path) {
        target_path_blocks_batch = !resolve_repo_root_auto_batch_scopes(req, paths, &count);
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
    if (!preserves_root) {
        pr_info("delegate repo-root batch skip: desc=%s reason=root_not_preserved",
                req->description[0] ? req->description : "-");
        return false;
    }

    resolved_paths = has_target_path ? !target_path_blocks_batch
                                     : resolve_repo_root_auto_batch_scopes(req, paths, &count);
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
    if (!resolve_repo_root_auto_batch_scopes(req, paths, &count)) {
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
    }
}
