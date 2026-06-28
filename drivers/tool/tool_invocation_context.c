/* 工具调用上下文补丁。
 * 在执行工具前，根据当前消息通道自动注入缺失的 channel/chat_id 参数。
 * 典型场景：cron add 时自动填充回复目标和飞书默认会话。 */

#include "drivers/tool/tool_invocation_context.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "drivers/channel/feishu/feishu_targets.h"
#include "drivers/tool/tool_file_ops.h"
#include "drivers/tool/tool_delegate_path_resolve.h"
#include "drivers/tool/tool_delegate_repo_batch.h"
#include "drivers/tool/tool_delegate_request.h"
#include "drivers/tool/tool_delegate_subagent.h"
#include "kernel/tooling/delegate/delegate_task_store.h"
#include "delegate/delegate_turn_directive.h"
#include "kernel/intent.h"
#include "cjson.h"
#include "linux/kernel.h"
#include "linux/printk.h"

static char *build_forced_delegate_preflight_batch(const struct message *msg);
static char *build_user_prompt_scoped_delegate_batch(const struct message *msg);
static char *build_user_prompt_generic_delegate_batch(const struct message *msg);
static char *build_llm_generic_delegate_batch(const struct message *msg);
static void json_set_string(cJSON *obj, const char *key, const char *value);

static bool invocation_text_has_work_signal(const char *line)
{
    static const char *const work_keywords[] = {
        "分析", "检查", "排查", "比较", "整理", "归纳", "验证", "实现", "修改", "修复",
        "重构", "编写", "补充", "梳理", "总结", "定位", "review", "analyze", "inspect",
        "check", "compare", "organize", "verify", "implement", "change", "fix",
        "refactor", "write", "summarize", "audit", "investigate"
    };

    if (!line || !line[0]) {
        return false;
    }
    for (size_t i = 0; i < ARRAY_SIZE(work_keywords); i++) {
        if (strstr(line, work_keywords[i])) {
            return true;
        }
    }
    return false;
}

static void trim_ascii_whitespace_inplace_local(char *text)
{
    size_t start = 0;
    size_t end;

    if (!text || !text[0]) {
        return;
    }

    end = strlen(text);
    while (text[start] == ' ' || text[start] == '\t' || text[start] == '\n' || text[start] == '\r') {
        start++;
    }
    while (end > start &&
           (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\n' || text[end - 1] == '\r')) {
        end--;
    }
    if (start > 0) {
        memmove(text, text + start, end - start);
    }
    text[end - start] = '\0';
}

static void strip_task_bullet_prefix_local(char *text)
{
    size_t idx = 0;

    if (!text || !text[0]) {
        return;
    }

    while (text[idx] == ' ' || text[idx] == '\t') {
        idx++;
    }
    while (text[idx] >= '0' && text[idx] <= '9') {
        idx++;
    }
    if ((idx > 0 && text[idx] == '.') ||
        text[idx] == '-' || text[idx] == '*' ||
        text[idx] == ')' || text[idx] == ':') {
        idx++;
    }
    while (text[idx] == ' ' || text[idx] == '\t') {
        idx++;
    }
    if (idx > 0) {
        memmove(text, text + idx, strlen(text + idx) + 1);
    }
    trim_ascii_whitespace_inplace_local(text);
}

static void parent_dir_of_path(const char *path, char *out, size_t out_size)
{
    const char *slash = NULL;
    size_t len = 0;

    if (!out || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (!path || !path[0]) {
        return;
    }
    slash = strrchr(path, '/');
    if (!slash || slash == path) {
        strscpy(out, "/", out_size);
        return;
    }
    len = (size_t)(slash - path);
    if (len >= out_size) {
        len = out_size - 1;
    }
    memcpy(out, path, len);
    out[len] = '\0';
}

static bool path_is_within_scope(const char *path, const char *scope_path)
{
    size_t scope_len;

    if (!path || !path[0] || !scope_path || !scope_path[0]) {
        return false;
    }
    scope_len = strlen(scope_path);
    if (strncmp(path, scope_path, scope_len) != 0) {
        return false;
    }
    return path[scope_len] == '\0' || path[scope_len] == '/';
}

bool tool_invocation_context_delegate_scope_path(const struct message *msg,
                                                 char *scope_path,
                                                 size_t scope_path_size)
{
    delegate_task_record_t task;

    if (!scope_path || scope_path_size == 0) {
        return false;
    }
    scope_path[0] = '\0';
    if (!msg || !tool_invocation_context_message_is_delegate_subagent(msg)) {
        return false;
    }
    if (delegate_task_store_find_by_session(msg->chat_id, &task, NULL) != 0) {
        return false;
    }
    if (!task.scope_path[0]) {
        return false;
    }
    strscpy(scope_path, task.scope_path, scope_path_size);
    return true;
}

static char *patch_delegate_subagent_files_input(const llm_tool_call_t *call, const struct message *msg)
{
    cJSON *root = NULL;
    char scope_path[512];
    char scope_anchor[512];
    char resolved_scope[1024];
    char resolved_path[1024];
    const char *action = NULL;
    const char *path = NULL;
    bool changed = false;
    char *patched = NULL;

    if (!call || !msg || !tool_invocation_context_delegate_scope_path(msg, scope_path, sizeof(scope_path))) {
        return NULL;
    }

    root = cJSON_Parse(call->input ? call->input : "{}");
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return NULL;
    }

    action = cJSON_GetStringValue(cJSON_GetObjectItem(root, "action"));
    path = cJSON_GetStringValue(cJSON_GetObjectItem(root, "path"));
    if (!action || !action[0]) {
        cJSON_Delete(root);
        return NULL;
    }

    strscpy(scope_anchor, scope_path, sizeof(scope_anchor));
    if (tool_files_resolve_read_path(scope_path, resolved_scope, sizeof(resolved_scope))) {
        strscpy(scope_anchor, resolved_scope, sizeof(scope_anchor));
    }

    if (strcmp(action, "list") == 0 || strcmp(action, "search") == 0) {
        if (!tool_files_resolve_read_path(scope_anchor, resolved_path, sizeof(resolved_path))) {
            resolved_path[0] = '\0';
        }
        if (resolved_path[0]) {
            struct stat st;
            if (stat(resolved_path, &st) == 0 && !S_ISDIR(st.st_mode)) {
                parent_dir_of_path(resolved_path, scope_anchor, sizeof(scope_anchor));
            } else {
                strscpy(scope_anchor, resolved_path, sizeof(scope_anchor));
            }
        }
    }

    if (!path || !path[0]) {
        json_set_string(root, "path", scope_anchor);
        changed = true;
    } else if (tool_files_resolve_read_path(path, resolved_path, sizeof(resolved_path)) &&
               !path_is_within_scope(resolved_path, scope_anchor)) {
        json_set_string(root, "path", scope_anchor);
        changed = true;
    }

    if (changed) {
        patched = cJSON_PrintUnformatted(root);
        if (patched) {
            pr_info("Patched delegate subagent files scope: chat=%s path=%s",
                    msg->chat_id,
                    scope_anchor);
        }
    }
    cJSON_Delete(root);
    return patched;
}

static char *patch_delegate_subagent_terminal_input(const llm_tool_call_t *call, const struct message *msg)
{
    cJSON *root = NULL;
    char scope_path[512];
    char workdir[512];
    char resolved_scope[1024];
    const char *command = NULL;
    bool changed = false;
    char *patched = NULL;

    if (!call || !msg || !tool_invocation_context_delegate_scope_path(msg, scope_path, sizeof(scope_path))) {
        return NULL;
    }

    root = cJSON_Parse(call->input ? call->input : "{}");
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return NULL;
    }

    strscpy(workdir, scope_path, sizeof(workdir));
    if (tool_files_resolve_read_path(scope_path, resolved_scope, sizeof(resolved_scope))) {
        struct stat st;
        if (stat(resolved_scope, &st) == 0 && !S_ISDIR(st.st_mode)) {
            parent_dir_of_path(resolved_scope, workdir, sizeof(workdir));
        } else {
            strscpy(workdir, resolved_scope, sizeof(workdir));
        }
    }
    json_set_string(root, "workdir", workdir);
    changed = true;

    command = cJSON_GetStringValue(cJSON_GetObjectItem(root, "command"));
    if (!command || !command[0]) {
        command = cJSON_GetStringValue(cJSON_GetObjectItem(root, "cmd"));
    }
    if (command && strstr(command, "cd ") == command && strstr(command, "&&")) {
        const char *after = strstr(command, "&&");
        if (after && after[2]) {
            while (after[2] == ' ') {
                after++;
            }
            json_set_string(root, "command", after + 2);
            changed = true;
        }
    }

    if (changed) {
        patched = cJSON_PrintUnformatted(root);
        if (patched) {
            pr_info("Patched delegate subagent terminal scope: chat=%s workdir=%s",
                    msg->chat_id,
                    workdir);
        }
    }
    cJSON_Delete(root);
    return patched;
}

static char *build_user_prompt_scoped_delegate_batch(const struct message *msg)
{
    return tool_delegate_build_user_prompt_scoped_delegate_batch_json(msg);
}

static char *build_user_prompt_generic_delegate_batch(const struct message *msg)
{
    return tool_delegate_build_user_prompt_generic_delegate_batch_json(msg);
}

static char *build_forced_parallel_delegate_batch_from_message(const struct message *msg)
{
    if (!msg || !msg->content || tool_invocation_context_message_is_delegate_subagent(msg)) {
        return NULL;
    }
    return build_user_prompt_scoped_delegate_batch(msg);
}

static char *build_forced_repo_parallel_delegate_batch(const struct message *msg)
{
    char *batch = NULL;

    if (!msg || !msg->content || tool_invocation_context_message_is_delegate_subagent(msg)) {
        return NULL;
    }

    batch = tool_delegate_build_repo_analysis_delegate_batch_json(msg);
    if (!batch) {
        batch = build_llm_generic_delegate_batch(msg);
    }
    if (!batch) {
        batch = build_user_prompt_generic_delegate_batch(msg);
    }
    if (!batch) {
        batch = build_forced_parallel_delegate_batch_from_message(msg);
    }
    return batch;
}

static char *load_delegate_turn_directive_json(const struct message *msg)
{
    char buf[16384];

    if (!msg || !msg->chat_id[0]) {
        return NULL;
    }
    if (!delegate_turn_directive_load_copy(msg->chat_id, buf, sizeof(buf)) || !buf[0]) {
        return NULL;
    }
    return strdup(buf);
}

static bool message_contains_any_keyword(const char *content,
                                         const char *const *keywords,
                                         size_t count)
{
    if (!content || !content[0] || !keywords || count == 0) {
        return false;
    }

    for (size_t i = 0; i < count; i++) {
        if (keywords[i] && keywords[i][0] && strstr(content, keywords[i])) {
            return true;
        }
    }
    return false;
}

static bool message_explicitly_disallows_parallel_subagents(const struct message *msg)
{
    const char *content;

    if (!msg || !msg->content) {
        return false;
    }

    content = msg->content;
    return strstr(content, "不要并行") != NULL ||
           strstr(content, "不用并行") != NULL ||
           strstr(content, "不必并行") != NULL ||
           strstr(content, "不要拆分") != NULL ||
           strstr(content, "不要安排多个subagent") != NULL ||
           strstr(content, "不要安排多个 subagent") != NULL ||
           strstr(content, "不要多个subagent") != NULL ||
           strstr(content, "不要多个 subagent") != NULL ||
           strstr(content, "不要多个子代理") != NULL ||
           strstr(content, "不要多 subagent") != NULL ||
           strstr(content, "do not parallel") != NULL ||
           strstr(content, "don't parallel") != NULL ||
           strstr(content, "do not use multiple subagents") != NULL ||
           strstr(content, "don't use multiple subagents") != NULL;
}

static int count_parallel_list_separators(const char *content)
{
    int count = 0;
    const char *cursor = content;

    if (!content) {
        return 0;
    }

    while ((cursor = strstr(cursor, "分别")) != NULL) {
        count++;
        cursor += strlen("分别");
    }
    cursor = content;
    while ((cursor = strstr(cursor, "以及")) != NULL) {
        count++;
        cursor += strlen("以及");
    }
    cursor = content;
    while ((cursor = strstr(cursor, " and ")) != NULL) {
        count++;
        cursor += 5;
    }
    cursor = content;
    while ((cursor = strstr(cursor, " then ")) != NULL) {
        count++;
        cursor += 6;
    }
    return count;
}

static bool message_has_multiple_explicit_targets(const struct message *msg)
{
    return tool_delegate_message_has_multiple_explicit_targets(msg);
}

static int count_multiline_work_items(const char *content)
{
    const char *cursor = content;
    int count = 0;

    if (!content || !content[0]) {
        return 0;
    }

    while (*cursor) {
        const char *line_end = strchr(cursor, '\n');
        size_t len = line_end ? (size_t)(line_end - cursor) : strlen(cursor);
        char line[320];

        if (len >= sizeof(line)) {
            len = sizeof(line) - 1;
        }
        memcpy(line, cursor, len);
        line[len] = '\0';
        trim_ascii_whitespace_inplace_local(line);
        strip_task_bullet_prefix_local(line);
        if (line[0] && invocation_text_has_work_signal(line)) {
            count++;
        }
        if (!line_end) {
            break;
        }
        cursor = line_end + 1;
    }

    return count;
}

static bool message_looks_like_complex_task(const struct message *msg)
{
    size_t len;

    if (!msg || !msg->content || !msg->content[0]) {
        return false;
    }
    if (message_has_multiple_explicit_targets(msg)) {
        return true;
    }
    if (count_multiline_work_items(msg->content) >= 2) {
        return true;
    }
    len = strlen(msg->content);
    if (msg->intent == INTENT_IMPLEMENT || msg->intent == INTENT_FIX || msg->intent == INTENT_INVESTIGATE) {
        if (len >= 140) {
            return true;
        }
        if (count_parallel_list_separators(msg->content) >= 1) {
            return true;
        }
    }
    return false;
}

static bool message_looks_like_repo_analysis_request(const struct message *msg)
{
    static const char *analysis_keywords[] = {
        "代码框架", "代码架构", "目录结构", "仓库", "项目", "模块", "入口", "主流程",
        "职责", "架构风险", "阅读顺序", "关键模块",
        "codebase", "repository", "repo", "architecture", "module", "entrypoint",
        "directory structure", "read order", "risk"
    };
    char repo_root[512];

    if (!msg || !msg->content || !msg->content[0]) {
        return false;
    }
    if (!tool_delegate_extract_single_absolute_repo_path(msg->content, repo_root, sizeof(repo_root)) ||
        !repo_root[0]) {
        return false;
    }
    return message_contains_any_keyword(msg->content,
                                        analysis_keywords,
                                        ARRAY_SIZE(analysis_keywords));
}

static bool message_looks_like_multi_workstream_request(const struct message *msg)
{
    static const char *keywords[] = {
        "分别", "各自", "同时", "并行", "拆成", "多块", "多部分", "多模块",
        "separately", "independently", "in parallel", "split into", "multiple"
    };
    static const char *task_keywords[] = {
        "分析", "检查", "排查", "比较", "整理", "归纳", "验证", "实现", "修改", "修复",
        "重构", "编写", "补充", "梳理", "总结", "定位", "review", "analyze", "inspect",
        "check", "compare", "organize", "verify", "implement", "change", "fix",
        "refactor", "write", "summarize", "audit"
    };
    bool has_parallel_hint;

    if (!msg || !msg->content) {
        return false;
    }

    if (message_has_multiple_explicit_targets(msg)) {
        return true;
    }
    has_parallel_hint = count_parallel_list_separators(msg->content) >= 1 ||
                        message_contains_any_keyword(msg->content, keywords, ARRAY_SIZE(keywords));
    if (has_parallel_hint &&
        message_contains_any_keyword(msg->content, task_keywords, ARRAY_SIZE(task_keywords))) {
        return true;
    }
    return false;
}

static bool message_requests_delegate_preflight_batch(const struct message *msg)
{
    if (!msg || !msg->content) {
        return false;
    }

    if (strstr(msg->content, "[Interview clarification answer]") == NULL) {
        return false;
    }

    if (strstr(msg->content, "sudo ls /root") == NULL &&
        strstr(msg->content, "/root") == NULL) {
        return false;
    }

    if (strstr(msg->content, "delegate_task") == NULL &&
        strstr(msg->content, "批量委托") == NULL) {
        return false;
    }

    if (strstr(msg->content, "tasks") == NULL &&
        strstr(msg->content, "tasks 数组") == NULL &&
        strstr(msg->content, "3 个子任务") == NULL &&
        strstr(msg->content, "子任务") == NULL) {
        return false;
    }

    return true;
}

bool tool_invocation_context_message_requests_multi_subagents(const struct message *msg)
{
    if (!msg || !msg->content) {
        return false;
    }
    if (message_explicitly_disallows_parallel_subagents(msg)) {
        return false;
    }

    static const char *keywords[] = {
        "多个subagent", "多个 subagent", "多 subagent", "多个子代理", "多个 agent",
        "同时安排", "分别分析", "并行", "批量", "批量委托", "拆成几个任务", "子任务",
        "multiple subagent", "multiple subagents", "parallel subagent",
        "parallel subagents", "batch delegate", "split into tasks"
    };
    for (size_t i = 0; i < sizeof(keywords) / sizeof(keywords[0]); i++) {
        if (strstr(msg->content, keywords[i])) {
            return true;
        }
    }
    return false;
}

bool tool_invocation_context_message_prefers_parallel_subagents(const struct message *msg)
{
    explicit_multi_scope_group_t explicit_scopes;

    if (!msg || !msg->content) {
        return false;
    }
    if (tool_invocation_context_message_is_delegate_subagent(msg)) {
        return false;
    }
    if (message_explicitly_disallows_parallel_subagents(msg)) {
        return false;
    }
    if (tool_delegate_collect_explicit_multi_scope_paths_from_message(msg, &explicit_scopes) &&
        explicit_scopes.count >= 2) {
        return true;
    }
    if (tool_invocation_context_message_requests_multi_subagents(msg)) {
        return true;
    }
    if (message_looks_like_multi_workstream_request(msg)) {
        return true;
    }
    return false;
}

bool tool_invocation_context_message_should_offer_delegate_tool(const struct message *msg)
{
    if (!msg || !msg->content) {
        return false;
    }
    if (tool_invocation_context_message_is_delegate_subagent(msg)) {
        return false;
    }
    if (message_explicitly_disallows_parallel_subagents(msg)) {
        return false;
    }
    if (tool_invocation_context_message_prefers_parallel_subagents(msg)) {
        return true;
    }
    return message_looks_like_complex_task(msg);
}

bool tool_invocation_context_message_is_delegate_subagent(const struct message *msg)
{
    if (!msg) {
        return false;
    }

    return strncmp(msg->chat_id, "delegate_sync_", 14) == 0;
}

static char *build_forced_delegate_preflight_batch(const struct message *msg)
{
    delegate_request_t req;
    char repo_root[512];

    if (!msg || !msg->content) {
        return NULL;
    }
    memset(&req, 0, sizeof(req));
    if (!tool_delegate_extract_single_absolute_repo_path(msg->content, repo_root, sizeof(repo_root)) ||
        !repo_root[0]) {
        return NULL;
    }
    strscpy(req.target_path, repo_root, sizeof(req.target_path));
    strscpy(req.description, "parallel delegated analysis", sizeof(req.description));
    strscpy(req.prompt, msg->content, sizeof(req.prompt));
    pr_info("Patched delegate_task to forced scoped batch+preflight for chat=%s", msg ? msg->chat_id : "");
    return tool_delegate_build_parallel_scope_batch_json(&req, true);
}

static bool delegate_task_input_is_single_explore(cJSON *root)
{
    const char *subagent_type = NULL;
    cJSON *tasks = NULL;

    if (!root || !cJSON_IsObject(root)) {
        return false;
    }

    subagent_type = cJSON_GetStringValue(cJSON_GetObjectItem(root, "subagent_type"));
    tasks = cJSON_GetObjectItem(root, "tasks");
    return subagent_type &&
           strcmp(subagent_type, "explore") == 0 &&
           (!tasks || !cJSON_IsArray(tasks));
}

static bool delegate_task_batch_item_looks_path_scoped(cJSON *item)
{
    const char *prompt = NULL;
    const char *target_path = NULL;

    if (!item || !cJSON_IsObject(item)) {
        return false;
    }

    prompt = cJSON_GetStringValue(cJSON_GetObjectItem(item, "prompt"));
    target_path = cJSON_GetStringValue(cJSON_GetObjectItem(item, "target_path"));
    if (!target_path || !target_path[0]) {
        return false;
    }
    if (!prompt || !prompt[0]) {
        return false;
    }

    return strstr(prompt, "围绕目标路径 `") != NULL ||
           strstr(prompt, "只负责目标路径 `") != NULL;
}

static bool delegate_task_input_looks_like_path_scoped_batch(cJSON *root)
{
    cJSON *tasks = NULL;
    cJSON *item = NULL;
    int count = 0;

    if (!root || !cJSON_IsObject(root)) {
        return false;
    }

    tasks = cJSON_GetObjectItem(root, "tasks");
    if (!tasks || !cJSON_IsArray(tasks) || cJSON_GetArraySize(tasks) < 2) {
        return false;
    }

    cJSON_ArrayForEach(item, tasks) {
        if (!delegate_task_batch_item_looks_path_scoped(item)) {
            return false;
        }
        count++;
    }

    return count >= 2;
}

static bool delegate_task_input_has_dispatch_mode(cJSON *root, const char *mode)
{
    const char *dispatch_mode;

    if (!root || !cJSON_IsObject(root) || !mode || !mode[0]) {
        return false;
    }
    dispatch_mode = cJSON_GetStringValue(cJSON_GetObjectItem(root, "dispatch_mode"));
    return dispatch_mode && strcmp(dispatch_mode, mode) == 0;
}

static bool delegate_task_batch_has_task_key(cJSON *root, const char *task_key)
{
    cJSON *tasks;
    cJSON *item = NULL;

    if (!root || !cJSON_IsObject(root) || !task_key || !task_key[0]) {
        return false;
    }
    tasks = cJSON_GetObjectItem(root, "tasks");
    if (!tasks || !cJSON_IsArray(tasks)) {
        return false;
    }
    cJSON_ArrayForEach(item, tasks) {
        const char *key = cJSON_GetStringValue(cJSON_GetObjectItem(item, "task_key"));
        if (key && strcmp(key, task_key) == 0) {
            return true;
        }
    }
    return false;
}

static bool delegate_json_task_item_valid(cJSON *item)
{
    const char *subagent_type;
    const char *prompt;

    if (!item || !cJSON_IsObject(item)) {
        return false;
    }
    subagent_type = cJSON_GetStringValue(cJSON_GetObjectItem(item, "subagent_type"));
    prompt = cJSON_GetStringValue(cJSON_GetObjectItem(item, "prompt"));
    if (!subagent_type || !subagent_type[0] || !prompt || !prompt[0]) {
        return false;
    }
    return tool_delegate_parse_subagent_kind(subagent_type) != DELEGATE_SUBAGENT_INVALID;
}

static bool delegate_json_batch_valid(const char *json_text)
{
    cJSON *root;
    cJSON *tasks;
    cJSON *item = NULL;
    int count = 0;

    if (!json_text || !json_text[0]) {
        return false;
    }

    root = cJSON_Parse(json_text);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }

    tasks = cJSON_GetObjectItem(root, "tasks");
    if (!tasks || !cJSON_IsArray(tasks)) {
        cJSON_Delete(root);
        return false;
    }

    cJSON_ArrayForEach(item, tasks) {
        if (!delegate_json_task_item_valid(item)) {
            cJSON_Delete(root);
            return false;
        }
        count++;
    }

    cJSON_Delete(root);
    return count >= 2;
}

static char *build_llm_generic_delegate_batch(const struct message *msg)
{
    cJSON *messages = NULL;
    cJSON *um = NULL;
    llm_response_t resp;
    char prompt[4096];

    if (!msg || !msg->content || !msg->content[0]) {
        return NULL;
    }

    snprintf(prompt,
             sizeof(prompt),
             "Turn the user request into a parallel delegate_task batch when parallel subagents are genuinely useful.\n"
             "Return JSON only.\n"
             "Schema:\n"
             "{\n"
             "  \"dispatch_mode\": \"parallel\" | \"single\",\n"
             "  \"tasks\": [\n"
             "    {\n"
             "      \"task_key\": \"short_id\",\n"
             "      \"subagent_type\": \"explore\" | \"librarian\" | \"oracle\" | \"implement\",\n"
             "      \"description\": \"short concrete label\",\n"
             "      \"prompt\": \"full self-contained task for that subagent\",\n"
             "      \"depends_on\": [\"optional_task_key\"]\n"
             "    }\n"
             "  ]\n"
             "}\n"
             "Rules:\n"
             "- Produce 2 to 4 tasks only when the request has independent or staged workstreams worth parallelizing.\n"
             "- If the request is better handled by one agent, return {\"dispatch_mode\":\"single\",\"tasks\":[]}.\n"
             "- Each task prompt must preserve the original goal and be directly executable.\n"
             "- Do not hardcode any repository path unless the user already provided it.\n"
             "- Do not rewrite the task into repository-structure analysis unless the user explicitly asked for that.\n"
             "- Use explore for code discovery, librarian for docs/config lookup, oracle for architecture judgement, implement for code changes.\n"
             "- Prefer staged dependencies over fake parallelism when one task must wait for another.\n"
             "- Descriptions must be generic task labels, not canned summaries.\n"
             "\n"
             "User intent: %s\n"
             "User request:\n%s",
             intent_name(msg->intent),
             msg->content);

    memset(&resp, 0, sizeof(resp));
    messages = cJSON_CreateArray();
    um = cJSON_CreateObject();
    if (!messages || !um) {
        cJSON_Delete(messages);
        cJSON_Delete(um);
        return NULL;
    }

    cJSON_AddStringToObject(um, "role", "user");
    cJSON_AddStringToObject(um, "content", prompt);
    cJSON_AddItemToArray(messages, um);

    if (llm_chat_tools_with_model_and_format(
            "You are a task decomposition assistant for multi-subagent orchestration.",
            messages,
            NULL,
            NULL,
            true,
            &resp) != 0) {
        cJSON_Delete(messages);
        llm_response_free(&resp);
        return NULL;
    }
    cJSON_Delete(messages);

    if (resp.text && delegate_json_batch_valid(resp.text)) {
        char *json = strdup(resp.text);
        llm_response_free(&resp);
        return json;
    }
    if (resp.reasoning_content && delegate_json_batch_valid(resp.reasoning_content)) {
        char *json = strdup(resp.reasoning_content);
        llm_response_free(&resp);
        return json;
    }

    llm_response_free(&resp);
    return NULL;
}

/* 安全设置 JSON 对象的字符串字段（先删后加）。 */
static void json_set_string(cJSON *obj, const char *key, const char *value)
{
    if (!obj || !key || !value) {
        return;
    }
    cJSON_DeleteItemFromObject(obj, key);
    cJSON_AddStringToObject(obj, key, value);
}

/**
 * 为 cron action=add 调用补全 channel 和 chat_id 参数。
 * - 若 channel 未指定则从消息上下文继承
 * - 若 chat_id 未指定且通道是 websocket，从消息上下文继承
 * - 若通道是飞书且 chat_id 未指定，使用飞书最近活跃会话
 * @return 修改后的 JSON 字符串（调用者需 kfree），无变化返回 NULL
 */
static char *patch_cron_action_add_target(const llm_tool_call_t *call, const struct message *msg)
{
    cJSON *root = cJSON_Parse(call->input ? call->input : "{}");
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        root = cJSON_CreateObject();
    }
    if (!root) {
        return NULL;
    }

    bool changed = false;

    cJSON *channel_item = cJSON_GetObjectItem(root, "channel");
    const char *channel = cJSON_IsString(channel_item) ? channel_item->valuestring : NULL;
    if ((!channel || channel[0] == '\0') && msg->channel[0] != '\0') {
        json_set_string(root, "channel", msg->channel);
        channel = msg->channel;
        changed = true;
    }

    cJSON *chat_item = cJSON_GetObjectItem(root, "chat_id");
    const char *chat_id = cJSON_IsString(chat_item) ? chat_item->valuestring : NULL;
    bool missing_chat_id = !chat_id || chat_id[0] == '\0' || strcmp(chat_id, "cron") == 0;

    if (channel && msg->channel[0] != '\0' &&
        strcmp(channel, msg->channel) == 0 && msg->chat_id[0] != '\0' && missing_chat_id) {
        json_set_string(root, "chat_id", msg->chat_id);
        changed = true;
        missing_chat_id = false;
    }

    if (channel && strcmp(channel, CHAN_FEISHU) == 0 && missing_chat_id) {
        char default_chat_id[64];
        if (feishu_targets_get_default(default_chat_id, sizeof(default_chat_id))) {
            json_set_string(root, "chat_id", default_chat_id);
            changed = true;
        }
    }

    char *patched = NULL;
    if (changed) {
        patched = cJSON_PrintUnformatted(root);
        if (patched) {
            const char *effective_channel = cJSON_GetStringValue(cJSON_GetObjectItem(root, "channel"));
            const char *effective_chat_id = cJSON_GetStringValue(cJSON_GetObjectItem(root, "chat_id"));
            pr_info("Patched cron add target to %s:%s", effective_channel ? effective_channel : "", effective_chat_id ? effective_chat_id : "");
        }
    }
    cJSON_Delete(root);
    return patched;
}

/**
 * 入口函数：根据工具名称决定是否需要对输入 JSON 进行上下文补丁。
 * 当前仅对 cron 工具的 action=add 操作生效。
 * @return 补丁后的 JSON 字符串（调用者释放），无变化返回 NULL
 */
char *tool_invocation_context_patch_input(const llm_tool_call_t *call, const struct message *msg)
{
    char *delegate_scope_patch = NULL;

    if (!call || !msg) {
        return NULL;
    }
    if (strcmp(call->name, "files") == 0 &&
        tool_invocation_context_message_is_delegate_subagent(msg)) {
        delegate_scope_patch = patch_delegate_subagent_files_input(call, msg);
        if (delegate_scope_patch) {
            return delegate_scope_patch;
        }
    }
    if (strcmp(call->name, "terminal") == 0 &&
        tool_invocation_context_message_is_delegate_subagent(msg)) {
        delegate_scope_patch = patch_delegate_subagent_terminal_input(call, msg);
        if (delegate_scope_patch) {
            return delegate_scope_patch;
        }
    }
    char *directive = NULL;
    if ((strcmp(call->name, "files") == 0 || strcmp(call->name, "terminal") == 0) &&
        !tool_invocation_context_message_is_delegate_subagent(msg) &&
        ((directive = load_delegate_turn_directive_json(msg)) != NULL ||
         message_requests_delegate_preflight_batch(msg))) {
        pr_info("Structured interview directive overrides %s with forced delegate batch for chat=%s",
                call->name,
                msg->chat_id);
        if (directive) {
            return directive;
        }
        return build_forced_delegate_preflight_batch(msg);
    }
    if (strcmp(call->name, "cron") == 0) {
        cJSON *root = cJSON_Parse(call->input ? call->input : "{}");
        const char *action = root ? cJSON_GetStringValue(cJSON_GetObjectItem(root, "action")) : NULL;
        bool should_patch = action && strcmp(action, "add") == 0;
        cJSON_Delete(root);
        if (!should_patch) {
            return NULL;
        }
        return patch_cron_action_add_target(call, msg);
    }
    if (strcmp(call->name, "delegate_task") == 0) {
        cJSON *root = cJSON_Parse(call->input ? call->input : "{}");
        bool has_stored_directive = false;
        char directive_buf[16384];
        const char *subagent_type = root ? cJSON_GetStringValue(cJSON_GetObjectItem(root, "subagent_type")) : NULL;
        cJSON *tasks = root ? cJSON_GetObjectItem(root, "tasks") : NULL;
        explicit_multi_scope_group_t explicit_scopes = {0};
        bool has_explicit_scope_paths = tool_delegate_collect_explicit_multi_scope_paths_from_message(msg,
                                                                                                      &explicit_scopes);
        bool has_valid_tasks = tasks && cJSON_IsArray(tasks) && cJSON_GetArraySize(tasks) >= 1;
        bool missing_delegate_shape = (!subagent_type || !subagent_type[0]) && !has_valid_tasks;
        bool should_force_batch = false;
        bool should_force_generic_parallel = false;
        bool should_rewrite_scoped_batch = false;
        bool should_rewrite_path_scoped_batch_to_generic = false;
        bool should_rewrite_invalid_delegate = false;
        bool should_rewrite_repo_analysis_direct_batch = false;
        char *scoped_batch = NULL;

        directive_buf[0] = '\0';
        if (!tool_invocation_context_message_is_delegate_subagent(msg)) {
            has_stored_directive = delegate_turn_directive_load_copy(msg->chat_id,
                                                                     directive_buf,
                                                                     sizeof(directive_buf));
        }
        should_force_batch = !has_stored_directive &&
                             message_requests_delegate_preflight_batch(msg) &&
                             delegate_task_input_is_single_explore(root);
        should_force_generic_parallel = !has_stored_directive &&
                                        delegate_task_input_is_single_explore(root) &&
                                        msg->intent == INTENT_INVESTIGATE &&
                                        message_looks_like_complex_task(msg) &&
                                        message_looks_like_repo_analysis_request(msg) &&
                                        !message_explicitly_disallows_parallel_subagents(msg);
        should_rewrite_scoped_batch = !has_stored_directive &&
                                      tasks && cJSON_IsArray(tasks) &&
                                      cJSON_GetArraySize(tasks) >= 2 &&
                                      tool_invocation_context_message_prefers_parallel_subagents(msg);
        should_rewrite_path_scoped_batch_to_generic = should_rewrite_scoped_batch &&
                                                      !has_explicit_scope_paths &&
                                                      delegate_task_input_looks_like_path_scoped_batch(root);
        should_rewrite_invalid_delegate = !has_stored_directive &&
                                          missing_delegate_shape &&
                                          tool_invocation_context_message_prefers_parallel_subagents(msg);
        should_rewrite_repo_analysis_direct_batch = !has_stored_directive &&
                                                    tasks && cJSON_IsArray(tasks) &&
                                                    cJSON_GetArraySize(tasks) >= 2 &&
                                                    msg->intent == INTENT_INVESTIGATE &&
                                                    message_looks_like_complex_task(msg) &&
                                                    message_looks_like_repo_analysis_request(msg) &&
                                                    !message_explicitly_disallows_parallel_subagents(msg) &&
                                                    (!delegate_task_input_has_dispatch_mode(root, "staged") ||
                                                     !delegate_task_batch_has_task_key(root, "oracle_synthesis"));
        pr_info("delegate_task patch check: chat=%s stored_directive=%d should_force_batch=%d should_force_generic_parallel=%d has_tasks=%d subagent_type=%s",
                msg->chat_id,
                has_stored_directive ? 1 : 0,
                should_force_batch ? 1 : 0,
                should_force_generic_parallel ? 1 : 0,
                (tasks && cJSON_IsArray(tasks)) ? 1 : 0,
                subagent_type ? subagent_type : "<null>");
        cJSON_Delete(root);
        if (has_stored_directive) {
            pr_info("Patched delegate_task to stored directive batch for chat=%s", msg->chat_id);
            return strdup(directive_buf);
        }
        if (should_force_batch) {
            return build_forced_delegate_preflight_batch(msg);
        }
        if (should_force_generic_parallel) {
            scoped_batch = build_forced_repo_parallel_delegate_batch(msg);
            if (scoped_batch) {
                pr_info("Patched single explore delegate_task to generic multi-subagent batch for chat=%s",
                        msg->chat_id);
                return scoped_batch;
            }
        }
        if (should_rewrite_invalid_delegate) {
            scoped_batch = build_forced_repo_parallel_delegate_batch(msg);
            if (scoped_batch) {
                pr_info("Patched invalid delegate_task to synthesized batch for chat=%s", msg->chat_id);
                return scoped_batch;
            }
        }
        if (should_rewrite_repo_analysis_direct_batch) {
            scoped_batch = build_forced_repo_parallel_delegate_batch(msg);
            if (scoped_batch) {
                pr_info("Patched repo-analysis delegate_task batch to staged+oracle for chat=%s",
                        msg->chat_id);
                return scoped_batch;
            }
        }
        if (should_rewrite_scoped_batch) {
            if (tool_invocation_context_message_is_delegate_subagent(msg)) {
                should_rewrite_scoped_batch = false;
            }
        }
        if (should_rewrite_path_scoped_batch_to_generic) {
            scoped_batch = build_forced_repo_parallel_delegate_batch(msg);
            if (scoped_batch) {
                pr_info("Patched path-scoped delegate_task batch back to generic user-prompt batch for chat=%s",
                        msg->chat_id);
                return scoped_batch;
            }
        }
        if (should_rewrite_scoped_batch && !should_rewrite_path_scoped_batch_to_generic) {
            pr_info("delegate_task patch keep existing multi-task batch for chat=%s", msg->chat_id);
        }
    }
    return NULL;
}

const char *tool_invocation_context_patch_tool_name(const llm_tool_call_t *call, const struct message *msg)
{
    if (!call || !msg) {
        return NULL;
    }
    return NULL;
}
