/* 工具执行：LLM 响应构建、工具结果组装 */
#include "turn_exec.h"
#include "tool_exec_fail.h"
#include "auto_verify.h"
#include "tool_feedback.h"
#include "tool_guard.h"
#include "linux/printk.h"
#include "text.h"
#include "turn_common.h"
#include "drivers/tool/tool_runtime.h"
#include "drivers/tool/tool_delegate.h"
#include "drivers/tool/tool_delegate_protocol.h"
#include "drivers/tool/tool_invocation_context.h"
#include "drivers/tool/tool_terminal_exec.h"
#include "work_item.h"
#include "linux/slab.h"
#include "linux/kernel.h"
#include "cjson.h"
#include "turn_dispatch.h"
#include <stdio.h>
#include <sys/stat.h>
#include <string.h>

typedef struct {
    int call_index;
    char id[64];
    char description[64];
    char prompt[2048];
    char subagent_type[24];
} delegate_batch_member_t;

typedef struct {
    int count;
    int primary_index;
    delegate_batch_member_t members[MAX_TOOL_CALLS];
} delegate_batch_group_t;

typedef struct {
    int call_index;
    char id[64];
    char path[512];
} files_batch_member_t;

typedef struct {
    int count;
    int primary_index;
    files_batch_member_t members[MAX_TOOL_CALLS];
} files_discovery_batch_group_t;

typedef struct {
    int count;
    char paths[MAX_TOOL_CALLS][512];
} explicit_multi_scope_group_t;

cJSON *agent_turn_build_assistant_content(const llm_response_t *resp)
{
    cJSON *content = cJSON_CreateArray();

    if (resp->reasoning_content && resp->reasoning_content_len > 0) {
        cJSON *block = cJSON_CreateObject();
        cJSON_AddStringToObject(block, "type", "reasoning");
        cJSON_AddStringToObject(block, "text", resp->reasoning_content);
        cJSON_AddItemToArray(content, block);
    }

    if (resp->text && resp->text_len > 0) {
        cJSON *block = cJSON_CreateObject();
        cJSON_AddStringToObject(block, "type", "text");
        cJSON_AddStringToObject(block, "text", resp->text);
        cJSON_AddItemToArray(content, block);
    }

    for (int i = 0; i < resp->call_count; i++) {
        const llm_tool_call_t *call = &resp->calls[i];
        cJSON *block = cJSON_CreateObject();
        cJSON_AddStringToObject(block, "type", "tool_use");
        cJSON_AddStringToObject(block, "id", call->id);
        cJSON_AddStringToObject(block, "name", call->name);
        cJSON *input = cJSON_Parse(call->input);
        cJSON_AddItemToObject(block, "input", input ? input : cJSON_CreateObject());
        if (!input) cJSON_AddItemToObject(block, "input", cJSON_CreateObject());
        cJSON_AddItemToArray(content, block);
    }
    return content;
}

char *agent_turn_generate_forced_final_response(const char *system_prompt,
                                                 cJSON *messages,
                                                 const char *reason,
                                                 bool response_format_json_object)
{
    if (!system_prompt || !messages) return NULL;

    cJSON *user_msg = cJSON_CreateObject();
    if (!user_msg) return NULL;
    cJSON_AddStringToObject(user_msg, "role", "user");

    const char *prefix = reason && reason[0] ? reason : "工具调用轮次已达上限。";
    const char *suffix = response_format_json_object
        ? "不要再调用任何工具。请仅基于当前已有的对话和工具结果，直接输出最终答复。必须只输出一个有效 JSON object，不要输出 markdown、解释、tool_calls、DSML、invoke、parameter。"
        : "不要再调用任何工具。请仅基于当前已有的对话和工具结果，直接输出最终答复。";
    size_t sz = strlen(prefix) + strlen(suffix) + 2;
    char *content = kzalloc(sz, GFP_KERNEL);
    if (!content) { cJSON_Delete(user_msg); return NULL; }
    snprintf(content, sz, "%s%s", prefix, suffix);
    cJSON_AddStringToObject(user_msg, "content", content);
    kfree(content);
    cJSON_AddItemToArray(messages, user_msg);

    llm_response_t resp;
    memset(&resp, 0, sizeof(resp));
    err_t err = llm_chat_tools_with_model_and_format(system_prompt,
                                                     messages,
                                                     NULL,
                                                     NULL,
                                                     response_format_json_object,
                                                     &resp);
    if (err != 0 || resp.tool_use || !resp.text || !resp.text[0]) {
        llm_response_free(&resp);
        return NULL;
    }
    char *final_text = strdup(resp.text);
    llm_response_free(&resp);
    return final_text;
}

static bool is_terminal_verification(const char *command)
{
    static const char *kw[] = {"cmake --build","ctest","make ","ninja ","pytest","npm test","go test","cargo test","cargo check"};
    for (size_t i = 0; i < sizeof(kw)/sizeof(kw[0]); i++)
        if (command && strstr(command, kw[i])) return true;
    return false;
}

static bool parse_delegate_batch_candidate(const llm_tool_call_t *call,
                                           delegate_batch_member_t *out)
{
    if (!call || !out || strcmp(call->name, "delegate_task") != 0) {
        return false;
    }

    cJSON *root = cJSON_Parse(call->input ? call->input : "{}");
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }

    const char *task_id = cJSON_GetStringValue(cJSON_GetObjectItem(root, "task_id"));
    const char *coordinator_id = cJSON_GetStringValue(cJSON_GetObjectItem(root, "coordinator_id"));
    const char *subagent_type = cJSON_GetStringValue(cJSON_GetObjectItem(root, "subagent_type"));
    const char *prompt = cJSON_GetStringValue(cJSON_GetObjectItem(root, "prompt"));
    const char *description = cJSON_GetStringValue(cJSON_GetObjectItem(root, "description"));

    bool ok = (!task_id || !task_id[0]) &&
              (!coordinator_id || !coordinator_id[0]) &&
              subagent_type && subagent_type[0] &&
              prompt && prompt[0];

    if (ok) {
        memset(out, 0, sizeof(*out));
        out->call_index = -1;
        strscpy(out->id, call->id, sizeof(out->id));
        strscpy(out->subagent_type, subagent_type, sizeof(out->subagent_type));
        strscpy(out->prompt, prompt, sizeof(out->prompt));
        strscpy(out->description,
                description && description[0] ? description : subagent_type,
                sizeof(out->description));
    }

    cJSON_Delete(root);
    return ok;
}

static bool parse_files_discovery_candidate(const llm_tool_call_t *call,
                                            files_batch_member_t *out)
{
    if (!call || !out || strcmp(call->name, "files") != 0) {
        return false;
    }

    cJSON *root = cJSON_Parse(call->input ? call->input : "{}");
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }

    const char *action = cJSON_GetStringValue(cJSON_GetObjectItem(root, "action"));
    const char *path = cJSON_GetStringValue(cJSON_GetObjectItem(root, "path"));
    cJSON *limit = cJSON_GetObjectItem(root, "limit");
    bool ok = action && strcmp(action, "list") == 0 && path && path[0];
    if (ok && limit && !cJSON_IsNumber(limit)) {
        ok = false;
    }

    if (ok) {
        memset(out, 0, sizeof(*out));
        out->call_index = -1;
        strscpy(out->id, call->id, sizeof(out->id));
        strscpy(out->path, path, sizeof(out->path));
    }

    cJSON_Delete(root);
    return ok;
}

static bool collect_delegate_batch_group(const llm_response_t *resp,
                                         delegate_batch_group_t *out)
{
    if (!resp || !out) {
        return false;
    }

    memset(out, 0, sizeof(*out));
    out->primary_index = -1;
    for (int i = 0; i < resp->call_count && out->count < MAX_TOOL_CALLS; i++) {
        delegate_batch_member_t member;
        if (!parse_delegate_batch_candidate(&resp->calls[i], &member)) {
            continue;
        }
        member.call_index = i;
        if (out->primary_index < 0) {
            out->primary_index = i;
        }
        out->members[out->count++] = member;
    }

    return out->count >= 2 && out->primary_index >= 0;
}

static bool collect_files_discovery_batch_group(const llm_response_t *resp,
                                                const struct message *msg,
                                                files_discovery_batch_group_t *out)
{
    if (!resp || !msg || !out) {
        return false;
    }
    if (!tool_invocation_context_message_requests_multi_subagents(msg)) {
        return false;
    }

    memset(out, 0, sizeof(*out));
    out->primary_index = -1;
    for (int i = 0; i < resp->call_count && out->count < MAX_TOOL_CALLS; i++) {
        files_batch_member_t member;
        if (!parse_files_discovery_candidate(&resp->calls[i], &member)) {
            continue;
        }
        member.call_index = i;
        if (out->primary_index < 0) {
            out->primary_index = i;
        }
        out->members[out->count++] = member;
    }

    return out->count >= 2 && out->primary_index >= 0;
}

static char *build_delegate_batch_input(const delegate_batch_group_t *group)
{
    if (!group || group->count < 2) {
        return NULL;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *tasks = cJSON_CreateArray();
    if (!root || !tasks) {
        cJSON_Delete(root);
        cJSON_Delete(tasks);
        return NULL;
    }

    for (int i = 0; i < group->count; i++) {
        const delegate_batch_member_t *member = &group->members[i];
        cJSON *item = cJSON_CreateObject();
        if (!item) {
            continue;
        }
        cJSON_AddStringToObject(item, "subagent_type", member->subagent_type);
        cJSON_AddStringToObject(item, "description", member->description);
        cJSON_AddStringToObject(item, "prompt", member->prompt);
        cJSON_AddItemToArray(tasks, item);
    }
    cJSON_AddItemToObject(root, "tasks", tasks);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

static const char *basename_after_last_slash(const char *path)
{
    const char *slash = path ? strrchr(path, '/') : NULL;
    return (slash && slash[1]) ? slash + 1 : path;
}

static char *build_files_discovery_delegate_batch_input(const files_discovery_batch_group_t *group)
{
    if (!group || group->count < 2) {
        return NULL;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *tasks = cJSON_CreateArray();
    if (!root || !tasks) {
        cJSON_Delete(root);
        cJSON_Delete(tasks);
        return NULL;
    }

    for (int i = 0; i < group->count; i++) {
        const files_batch_member_t *member = &group->members[i];
        const char *leaf = basename_after_last_slash(member->path);
        char description[96];
        char prompt[1024];
        snprintf(description, sizeof(description), "分析 %s 模块结构", leaf && leaf[0] ? leaf : member->path);
        snprintf(prompt, sizeof(prompt),
                 "分析 %s 的目录结构和关键模块。"
                 "只做代表性覆盖，不要穷举。"
                 "先总结直接子目录和主要职责，再读取少量代表性文件说明入口、主链和下一步应关注的文件。",
                 member->path);

        cJSON *item = cJSON_CreateObject();
        if (!item) {
            continue;
        }
        cJSON_AddStringToObject(item, "subagent_type", "explore");
        cJSON_AddStringToObject(item, "description", description);
        cJSON_AddStringToObject(item, "prompt", prompt);
        cJSON_AddStringToObject(item, "target_path", member->path);
        cJSON_AddItemToArray(tasks, item);
    }

    cJSON_AddItemToObject(root, "tasks", tasks);
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

static bool explicit_scope_exists(const explicit_multi_scope_group_t *group, const char *path)
{
    if (!group || !path || !path[0]) {
        return false;
    }
    for (int i = 0; i < group->count; i++) {
        if (strcmp(group->paths[i], path) == 0) {
            return true;
        }
    }
    return false;
}

static void explicit_scope_add(explicit_multi_scope_group_t *group, const char *path)
{
    if (!group || !path || !path[0] || group->count >= MAX_TOOL_CALLS) {
        return;
    }
    if (explicit_scope_exists(group, path)) {
        return;
    }
    strscpy(group->paths[group->count++], path, sizeof(group->paths[0]));
}

static bool explicit_scope_is_separator(char ch)
{
    return ch == '\0' || ch == ' ' || ch == '\n' || ch == '\t' ||
           ch == ',' || ch == ':' || ch == ';' || ch == ')' ||
           ch == '(' || ch == '"' ||
           ch == '\'' || ch == '`';
}

static bool explicit_scope_is_token_char(char ch)
{
    return (ch >= 'a' && ch <= 'z') ||
           (ch >= 'A' && ch <= 'Z') ||
           (ch >= '0' && ch <= '9') ||
           ch == '_' || ch == '-' || ch == '/';
}

static void trim_scope_token(char *token)
{
    size_t len;

    if (!token || !token[0]) {
        return;
    }

    while (*token == '`' || *token == '"' || *token == '\'' ||
           *token == '(') {
        memmove(token, token + 1, strlen(token));
    }

    len = strlen(token);
    while (len > 0) {
        char ch = token[len - 1];
        if (ch != '/' && !explicit_scope_is_separator(ch)) {
            break;
        }
        token[--len] = '\0';
    }
}

static bool explicit_scope_path_exists(const char *path)
{
    struct stat st;
    return path && path[0] && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static void extract_first_absolute_repo_path(const char *content, char *root, size_t root_size)
{
    const char *start;

    if (!root || root_size == 0) {
        return;
    }
    root[0] = '\0';
    if (!content || !content[0]) {
        return;
    }

    start = strchr(content, '/');
    while (start) {
        const char *end = start;
        while (*end && !explicit_scope_is_separator(*end)) {
            end++;
        }
        if ((size_t)(end - start) > 1 && (size_t)(end - start) < root_size) {
            memcpy(root, start, (size_t)(end - start));
            root[end - start] = '\0';
            trim_scope_token(root);
            if (root[0] && explicit_scope_path_exists(root)) {
                return;
            }
        }
        start = strchr(end, '/');
    }
    root[0] = '\0';
}

static void maybe_add_relative_scope_from_keyword(const char *content,
                                                  const char *root,
                                                  explicit_multi_scope_group_t *out,
                                                  const char *needle)
{
    const char *pos = content;

    if (!content || !root || !root[0] || !out || !needle || !needle[0]) {
        return;
    }

    while ((pos = strstr(pos, needle)) != NULL) {
        char before = (pos == content) ? '\0' : pos[-1];
        char after = pos[strlen(needle)];
        char path[512];

        if ((before == '/' && pos > content) || before == '_' || before == '-') {
            pos += strlen(needle);
            continue;
        }
        if (explicit_scope_is_token_char(before) && before != '/') {
            pos += strlen(needle);
            continue;
        }
        if (explicit_scope_is_token_char(after) && after != '/') {
            pos += strlen(needle);
            continue;
        }
        if (!strchr(needle, '/') && after == '/') {
            pos += strlen(needle);
            continue;
        }

        snprintf(path, sizeof(path), "%s/%s", root, needle);
        trim_scope_token(path);
        if (explicit_scope_path_exists(path)) {
            explicit_scope_add(out, path);
        }
        pos += strlen(needle);
    }
}

static void extract_relative_scope_paths(const char *content,
                                         const char *root,
                                         explicit_multi_scope_group_t *out)
{
    const char *relative_markers[] = {
        "kernel/turn",
        "kernel/tooling",
        "kernel/context",
        "kernel/router",
        "kernel/turning",
        "kernel",
        "drivers/tool",
        "drivers/llm",
        "drivers/channel",
        "drivers",
        "docs",
    };

    if (!content || !root || !root[0] || !out) {
        return;
    }

    for (size_t i = 0; i < sizeof(relative_markers) / sizeof(relative_markers[0]); i++) {
        maybe_add_relative_scope_from_keyword(content, root, out, relative_markers[i]);
    }
}

static void extract_absolute_scope_paths(const char *content,
                                         const char *repo_root,
                                         explicit_multi_scope_group_t *out)
{
    const char *start = content;

    if (!content || !out) {
        return;
    }

    while ((start = strchr(start, '/')) != NULL) {
        const char *end = start;
        char path[512];

        while (*end && !explicit_scope_is_separator(*end)) {
            end++;
        }
        if ((size_t)(end - start) <= 1 || (size_t)(end - start) >= sizeof(path)) {
            start = end;
            continue;
        }

        memcpy(path, start, (size_t)(end - start));
        path[end - start] = '\0';
        trim_scope_token(path);
        if (strchr(path + 1, '/') &&
            explicit_scope_path_exists(path) &&
            (!repo_root || !repo_root[0] || strcmp(path, repo_root) != 0)) {
            explicit_scope_add(out, path);
        }
        start = end;
    }
}

static bool collect_explicit_multi_scope_paths(const struct message *msg,
                                               explicit_multi_scope_group_t *out)
{
    const char *content;
    char root[512];

    if (!msg || !out) {
        return false;
    }
    if (!tool_invocation_context_message_requests_multi_subagents(msg)) {
        return false;
    }

    memset(out, 0, sizeof(*out));
    content = msg->content ? msg->content : "";

    extract_first_absolute_repo_path(content, root, sizeof(root));
    extract_absolute_scope_paths(content, root, out);
    extract_relative_scope_paths(content, root, out);

    return out->count >= 2;
}

static char *build_explicit_multi_scope_delegate_batch_input(const explicit_multi_scope_group_t *group)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *tasks = cJSON_CreateArray();
    if (!group || group->count < 2 || !root || !tasks) {
        cJSON_Delete(root);
        cJSON_Delete(tasks);
        return NULL;
    }

    for (int i = 0; i < group->count; i++) {
        const char *path = group->paths[i];
        const char *leaf = basename_after_last_slash(path);
        char description[96];
        char prompt[1024];
        cJSON *item = cJSON_CreateObject();
        if (!item) {
            continue;
        }
        snprintf(description, sizeof(description), "分析 %s 目录结构", leaf && leaf[0] ? leaf : path);
        snprintf(prompt, sizeof(prompt),
                 "分析 %s 的目录结构和关键模块。"
                 "只做代表性覆盖，不要穷举。"
                 "总结直接子目录、核心文件、主要职责，以及下一步值得继续看的文件。",
                 path);
        cJSON_AddStringToObject(item, "subagent_type", "explore");
        cJSON_AddStringToObject(item, "description", description);
        cJSON_AddStringToObject(item, "prompt", prompt);
        cJSON_AddStringToObject(item, "target_path", path);
        cJSON_AddItemToArray(tasks, item);
    }

    cJSON_AddItemToObject(root, "tasks", tasks);
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

static char *extract_coordinator_id_from_output(const char *tool_output)
{
    if (!tool_output || !tool_output[0]) {
        return NULL;
    }
    cJSON *root = cJSON_Parse(tool_output);
    if (!root) {
        return NULL;
    }
    const char *coordinator_id = cJSON_GetStringValue(cJSON_GetObjectItem(root, "coordinator_id"));
    char *result = coordinator_id && coordinator_id[0] ? strdup(coordinator_id) : NULL;
    cJSON_Delete(root);
    return result;
}

static char *build_merged_delegate_result(const char *coordinator_id,
                                          const char *primary_tool_use_id,
                                          const char *subagent_type)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return NULL;
    }
    cJSON_AddStringToObject(root, "status", "merged_into_batch");
    if (coordinator_id && coordinator_id[0]) {
        cJSON_AddStringToObject(root, "coordinator_id", coordinator_id);
    }
    if (primary_tool_use_id && primary_tool_use_id[0]) {
        cJSON_AddStringToObject(root, "primary_tool_use_id", primary_tool_use_id);
    }
    if (subagent_type && subagent_type[0]) {
        cJSON_AddStringToObject(root, "subagent_type", subagent_type);
    }
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

static char *build_merged_sync_delegate_result(const char *primary_tool_use_id,
                                               const char *summary)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return NULL;
    }
    cJSON_AddStringToObject(root, "status", "merged_into_sync_delegate");
    cJSON_AddStringToObject(root, "delivery", "sync_final");
    if (primary_tool_use_id && primary_tool_use_id[0]) {
        cJSON_AddStringToObject(root, "primary_tool_use_id", primary_tool_use_id);
    }
    if (summary && summary[0]) {
        cJSON_AddStringToObject(root, "output", summary);
    }
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

static void maybe_mark_background_delegate_started(turn_exec_stats_t *stats,
                                                   const char *tool_name,
                                                   const char *tool_output)
{
    if (!stats || !tool_name || strcmp(tool_name, "delegate_task") != 0 || stats->background_delegate_started) {
        return;
    }
    char *coordinator_id = extract_coordinator_id_from_output(tool_output);
    if (!coordinator_id || !coordinator_id[0]) {
        free(coordinator_id);
        return;
    }
    stats->background_delegate_started = true;
    strscpy(stats->background_delegate_coordinator_id,
            coordinator_id,
            sizeof(stats->background_delegate_coordinator_id));
    snprintf(stats->background_delegate_reply,
             sizeof(stats->background_delegate_reply),
             "已启动后台子任务，coordinator_id=%s。后续进度和完成结果将通过实时事件返回。",
             coordinator_id);
    free(coordinator_id);
}

static void set_background_delegate_started(turn_exec_stats_t *stats,
                                            const char *coordinator_id,
                                            int task_count)
{
    if (!stats) {
        return;
    }

    stats->background_delegate_started = true;
    strscpy(stats->background_delegate_coordinator_id,
            coordinator_id ? coordinator_id : "",
            sizeof(stats->background_delegate_coordinator_id));
    if (task_count > 1) {
        snprintf(stats->background_delegate_reply,
                 sizeof(stats->background_delegate_reply),
                 "已启动 %d 个后台子任务，coordinator_id=%s。后续进度和完成结果将通过实时事件返回。",
                 task_count,
                 coordinator_id && coordinator_id[0] ? coordinator_id : "unknown");
    } else {
        snprintf(stats->background_delegate_reply,
                 sizeof(stats->background_delegate_reply),
                 "已启动后台子任务，coordinator_id=%s。后续进度和完成结果将通过实时事件返回。",
                 coordinator_id && coordinator_id[0] ? coordinator_id : "unknown");
    }
}

static bool should_merge_into_existing_background_delegate(const turn_exec_stats_t *stats,
                                                           const llm_tool_call_t *call,
                                                           const struct message *msg)
{
    if (!stats || !call || !msg || !stats->background_delegate_started) {
        return false;
    }
    if (!stats->background_delegate_coordinator_id[0]) {
        return false;
    }

    if (strcmp(call->name, "files") == 0) {
        const char *patched_tool_name = tool_invocation_context_patch_tool_name(call, msg);
        return patched_tool_name && strcmp(patched_tool_name, "delegate_task") == 0;
    }

    if (strcmp(call->name, "terminal") == 0) {
        cJSON *root = cJSON_Parse(call->input ? call->input : "{}");
        const char *command = root ? cJSON_GetStringValue(cJSON_GetObjectItem(root, "command")) : NULL;
        bool should_delegate = tool_invocation_context_terminal_command_looks_broad_discovery(command, msg);
        cJSON_Delete(root);
        return should_delegate;
    }

    return false;
}

void agent_turn_maybe_mark_sync_delegate_completed(turn_exec_stats_t *stats,
                                                   const char *tool_name,
                                                   const char *tool_output)
{
    char summary[sizeof(stats->sync_delegate_reply)];

    if (!stats || !tool_name || strcmp(tool_name, "delegate_task") != 0 ||
        stats->background_delegate_started || stats->sync_delegate_completed) {
        return;
    }
    if (!tool_delegate_extract_sync_final_output(tool_output, summary, sizeof(summary))) {
        return;
    }

    stats->sync_delegate_completed = true;
    strscpy(stats->sync_delegate_reply, summary, sizeof(stats->sync_delegate_reply));
}

static void append_tool_result_block(cJSON *content,
                                     const char *tool_use_id,
                                     const char *result_text)
{
    cJSON *block = cJSON_CreateObject();
    if (!content || !block) {
        cJSON_Delete(block);
        return;
    }
    cJSON_AddStringToObject(block, "type", "tool_result");
    cJSON_AddStringToObject(block, "tool_use_id", tool_use_id ? tool_use_id : "");
    cJSON_AddStringToObject(block, "content", result_text ? result_text : "");
    cJSON_AddItemToArray(content, block);
}

cJSON *agent_turn_build_tool_results(const llm_response_t *resp,
                                      const struct message *msg,
                                      const char *tools_json,
                                      char *tool_output, size_t tool_output_size,
                                      turn_exec_stats_t *stats)
{
    cJSON *content = cJSON_CreateArray();
    delegate_batch_group_t batch_group;
    bool has_delegate_batch = collect_delegate_batch_group(resp, &batch_group);
    files_discovery_batch_group_t files_batch_group;
    bool has_files_discovery_batch = collect_files_discovery_batch_group(resp, msg, &files_batch_group);
    char *batch_output = NULL;
    char *batch_coordinator_id = NULL;
    char *sync_delegate_tool_output = NULL;
    char sync_delegate_primary_tool_id[64] = {0};
    err_t batch_err = 0;
    bool batch_executed = false;
    explicit_multi_scope_group_t explicit_scope_group;
    bool has_explicit_scope_group = collect_explicit_multi_scope_paths(msg, &explicit_scope_group);

    for (int i = 0; i < resp->call_count; i++) {
        const llm_tool_call_t *call = &resp->calls[i];
        const char *tool_input = call->input ? call->input : "{}";
        tool_runtime_result_t rt = {0};
        bool tool_advertised = agent_tool_name_is_advertised(tools_json, call->name);

        if (!tool_advertised) {
            snprintf(tool_output,
                     tool_output_size,
                     "tool protocol error: tool '%s' was not advertised for this turn",
                     call->name);
            pr_warn("Rejected non-advertised tool call: chat=%s source=%s tool=%s",
                    msg->chat_id,
                    agent_msg_source_or_default(msg),
                    call->name);
            if (stats) {
                stats->unrecoverable_tool_protocol_error = true;
                snprintf(stats->tool_protocol_error_reason,
                         sizeof(stats->tool_protocol_error_reason),
                         "工具 %s 不在当前轮允许工具集中",
                         call->name);
            }
            append_tool_result_block(content, call->id, tool_output);
            continue;
        }

        if (has_explicit_scope_group &&
            i == 0 &&
            strcmp(call->name, "files") == 0 &&
            !has_files_discovery_batch &&
            !has_delegate_batch) {
            char *batch_input = build_explicit_multi_scope_delegate_batch_input(&explicit_scope_group);
            llm_tool_call_t merged_call;
            memset(&merged_call, 0, sizeof(merged_call));
            strscpy(merged_call.id, call->id, sizeof(merged_call.id));
            strscpy(merged_call.name, "delegate_task", sizeof(merged_call.name));
            merged_call.input = batch_input;
            merged_call.input_len = batch_input ? strlen(batch_input) : 0;
            log_tool_payload_preview("before_runtime", msg, "delegate_task", call->id, batch_input, NULL, 0);
            batch_err = tool_runtime_execute_call(&merged_call, msg, tool_output, tool_output_size, &rt);
            batch_executed = true;
            if (rt.effective_input) tool_input = rt.effective_input;
            log_tool_payload_preview(rt.effective_input ? "after_runtime_patched" : "after_runtime",
                                     msg, "delegate_task", call->id, tool_input, tool_output, batch_err);
            record_turn_side_effects(stats, "delegate_task", tool_input);
            agent_tool_feedback_send_activity(msg, "delegate_task", tool_input, tool_output, batch_err, rt.elapsed_ms);
            collect_tool_failure_work_item(msg, "delegate_task", tool_input, tool_output, batch_err);
                if (batch_err == 0) {
                    batch_coordinator_id = extract_coordinator_id_from_output(tool_output);
                    set_background_delegate_started(stats,
                                                    batch_coordinator_id,
                                                    explicit_scope_group.count);
                    pr_info("delegate explicit-scope batch merged: primary=%s merged_scopes=%d coordinator=%s",
                            call->id,
                            explicit_scope_group.count,
                        batch_coordinator_id ? batch_coordinator_id : "<missing>");
            }
            text_sanitize_utf8_json(tool_output);
            append_tool_result_block(content, call->id, tool_output);
            kfree(rt.effective_input);
            kfree(batch_input);
            continue;
        }

        if (has_files_discovery_batch && strcmp(call->name, "files") == 0) {
            bool is_batch_member = false;
            const files_batch_member_t *member = NULL;
            for (int j = 0; j < files_batch_group.count; j++) {
                if (files_batch_group.members[j].call_index == i) {
                    is_batch_member = true;
                    member = &files_batch_group.members[j];
                    break;
                }
            }
            if (is_batch_member && i != files_batch_group.primary_index) {
                char *merged = build_merged_delegate_result(batch_coordinator_id,
                                                            resp->calls[files_batch_group.primary_index].id,
                                                            "explore");
                append_tool_result_block(content, call->id, merged ? merged : "{\"status\":\"merged_into_batch\"}");
                kfree(merged);
                continue;
            }
            if (is_batch_member && i == files_batch_group.primary_index) {
                char *batch_input = build_files_discovery_delegate_batch_input(&files_batch_group);
                llm_tool_call_t merged_call;
                memset(&merged_call, 0, sizeof(merged_call));
                strscpy(merged_call.id, call->id, sizeof(merged_call.id));
                strscpy(merged_call.name, "delegate_task", sizeof(merged_call.name));
                merged_call.input = batch_input;
                merged_call.input_len = batch_input ? strlen(batch_input) : 0;
                log_tool_payload_preview("before_runtime", msg, "delegate_task", call->id, batch_input, NULL, 0);
                batch_err = tool_runtime_execute_call(&merged_call, msg, tool_output, tool_output_size, &rt);
                batch_executed = true;
                if (rt.effective_input) tool_input = rt.effective_input;
                log_tool_payload_preview(rt.effective_input ? "after_runtime_patched" : "after_runtime",
                                         msg, "delegate_task", call->id, tool_input, tool_output, batch_err);
                record_turn_side_effects(stats, "delegate_task", tool_input);
                agent_tool_feedback_send_activity(msg, "delegate_task", tool_input, tool_output, batch_err, rt.elapsed_ms);
                collect_tool_failure_work_item(msg, "delegate_task", tool_input, tool_output, batch_err);
                if (batch_err == 0) {
                    batch_coordinator_id = extract_coordinator_id_from_output(tool_output);
                    batch_output = strdup(tool_output);
                    set_background_delegate_started(stats,
                                                    batch_coordinator_id,
                                                    files_batch_group.count);
                    pr_info("delegate files-discovery batch merged: primary=%s merged_calls=%d coordinator=%s",
                            call->id,
                            files_batch_group.count,
                            batch_coordinator_id ? batch_coordinator_id : "<missing>");
                }
                text_sanitize_utf8_json(tool_output);
                append_tool_result_block(content, call->id, tool_output);
                kfree(rt.effective_input);
                kfree(batch_input);
                continue;
            }
        }

        if (sync_delegate_tool_output &&
            strcmp(call->name, "files") == 0 &&
            stats && stats->sync_delegate_completed) {
            const char *patched_tool_name = tool_invocation_context_patch_tool_name(call, msg);
            if (patched_tool_name && strcmp(patched_tool_name, "delegate_task") == 0) {
                char *merged = build_merged_sync_delegate_result(sync_delegate_primary_tool_id,
                                                                 stats->sync_delegate_reply);
                pr_info("delegate sync merged duplicate broad-discovery call: tool_use_id=%s primary=%s",
                        call->id,
                        sync_delegate_primary_tool_id[0] ? sync_delegate_primary_tool_id : "<missing>");
                append_tool_result_block(content,
                                         call->id,
                                         merged ? merged : sync_delegate_tool_output);
                kfree(merged);
                continue;
            }
        }

        if (should_merge_into_existing_background_delegate(stats, call, msg)) {
            char *merged = build_merged_delegate_result(stats->background_delegate_coordinator_id,
                                                        "",
                                                        "explore");
            pr_info("delegate turn-level duplicate broad-discovery call merged: tool=%s tool_use_id=%s coordinator=%s",
                    call->name,
                    call->id,
                    stats->background_delegate_coordinator_id);
            append_tool_result_block(content,
                                     call->id,
                                     merged ? merged : "{\"status\":\"merged_into_batch\"}");
            kfree(merged);
            continue;
        }

        if (has_delegate_batch && strcmp(call->name, "delegate_task") == 0) {
            bool is_batch_member = false;
            const delegate_batch_member_t *member = NULL;
            for (int j = 0; j < batch_group.count; j++) {
                if (batch_group.members[j].call_index == i) {
                    is_batch_member = true;
                    member = &batch_group.members[j];
                    break;
                }
            }
            if (is_batch_member && i != batch_group.primary_index) {
                char *merged = build_merged_delegate_result(batch_coordinator_id,
                                                            resp->calls[batch_group.primary_index].id,
                                                            member ? member->subagent_type : "");
                append_tool_result_block(content, call->id, merged ? merged : "{\"status\":\"merged_into_batch\"}");
                kfree(merged);
                continue;
            }
            if (is_batch_member && i == batch_group.primary_index) {
                char *batch_input = build_delegate_batch_input(&batch_group);
                llm_tool_call_t merged_call = *call;
                merged_call.input = batch_input;
                log_tool_payload_preview("before_runtime", msg, "delegate_task", call->id, batch_input, NULL, 0);
                batch_err = tool_runtime_execute_call(&merged_call, msg, tool_output, tool_output_size, &rt);
                batch_executed = true;
                if (rt.effective_input) tool_input = rt.effective_input;
                log_tool_payload_preview(rt.effective_input ? "after_runtime_patched" : "after_runtime",
                                         msg, "delegate_task", call->id, tool_input, tool_output, batch_err);
                record_turn_side_effects(stats, "delegate_task", tool_input);
                agent_tool_feedback_send_activity(msg, "delegate_task", tool_input, tool_output, batch_err, rt.elapsed_ms);
                collect_tool_failure_work_item(msg, "delegate_task", tool_input, tool_output, batch_err);
                if (batch_err == 0) {
                    batch_coordinator_id = extract_coordinator_id_from_output(tool_output);
                    batch_output = strdup(tool_output);
                    set_background_delegate_started(stats,
                                                    batch_coordinator_id,
                                                    batch_group.count);
                    pr_info("delegate batch merged: primary=%s merged_calls=%d coordinator=%s",
                            call->id,
                            batch_group.count,
                            batch_coordinator_id ? batch_coordinator_id : "<missing>");
                }
                text_sanitize_utf8_json(tool_output);
                append_tool_result_block(content, call->id, tool_output);
                kfree(rt.effective_input);
                kfree(batch_input);
                continue;
            }
        }

        log_tool_payload_preview("before_runtime", msg, call->name, call->id, tool_input, NULL, 0);

        err_t tool_err = tool_runtime_execute_call(call, msg,
                                                    tool_output, tool_output_size, &rt);
        const char *effective_tool_name = (rt.effective_tool_name && rt.effective_tool_name[0])
                                              ? rt.effective_tool_name
                                              : call->name;
        if (rt.effective_input) tool_input = rt.effective_input;
        bool recoverable_tool_noise =
            agent_tool_failure_is_recoverable_noise(effective_tool_name, tool_input, tool_output, tool_err);

        log_tool_payload_preview(rt.effective_input ? "after_runtime_patched" : "after_runtime",
                                 msg, effective_tool_name, call->id, tool_input, tool_output, tool_err);

        record_turn_side_effects(stats, effective_tool_name, tool_input);
        if (!recoverable_tool_noise) {
            agent_tool_feedback_send_activity(msg, effective_tool_name, tool_input, tool_output, tool_err, rt.elapsed_ms);
        } else {
            pr_info("Recoverable tool noise ignored: tool=%s input=%s output=%s",
                    effective_tool_name ? effective_tool_name : "-",
                    tool_input ? tool_input : "{}",
                    tool_output ? tool_output : "");
        }
        collect_tool_failure_work_item(msg, effective_tool_name, tool_input, tool_output, tool_err);

        if (agent_tool_protocol_failure_should_stop(effective_tool_name, tool_input, tool_output, tool_err)) {
            stats->unrecoverable_tool_protocol_error = true;
            snprintf(stats->tool_protocol_error_reason, sizeof(stats->tool_protocol_error_reason),
                     "工具 %s 收到无效协议参数 input=%s", effective_tool_name, tool_input ? tool_input : "{}");
        }

        if (strcmp(effective_tool_name, "terminal") == 0 && tool_input && is_terminal_verification(tool_input)) {
            cJSON *tr = cJSON_Parse(tool_output);
            if (tr) {
                cJSON *ec = cJSON_GetObjectItem(tr, "exit_code");
                cJSON *to = cJSON_GetObjectItem(tr, "timed_out");
                if ((ec && cJSON_IsNumber(ec) && ec->valueint != 0) ||
                    (to && cJSON_IsBool(to) && cJSON_IsTrue(to))) {
                    char title[256];
                    snprintf(title, sizeof(title), "验证命令失败: %.200s", tool_input);
                    work_item_store_collect("defect", "test", title, tool_output);
                }
                cJSON_Delete(tr);
            }
        } else if (strcmp(effective_tool_name, "webfetch") == 0 && tool_err != 0) {
            cJSON *wf = cJSON_Parse(tool_input);
            const char *url = NULL;
            if (wf) url = cJSON_GetStringValue(cJSON_GetObjectItem(wf, "url"));
            if (url && url[0]) {
                char title[256];
                snprintf(title, sizeof(title), "webfetch 失败: %.200s", url);
                work_item_store_collect("defect", "test", title, tool_output);
            }
            cJSON_Delete(wf);
        }

        if (tool_err == 0)
            pr_info("Tool %s result: %d bytes", effective_tool_name, (int)strlen(tool_output));
        else if (!recoverable_tool_noise) {
            char ip[240], op[240];
            text_shorten(tool_input, ip, sizeof(ip), 220);
            text_shorten(tool_output, op, sizeof(op), 220);
            pr_warn("Tool %s failed: %s input=%s output=%s", effective_tool_name, err_name(tool_err), ip, op);
        }

        text_sanitize_utf8_json(tool_output);
        maybe_mark_background_delegate_started(stats, effective_tool_name, tool_output);
        agent_turn_maybe_mark_sync_delegate_completed(stats, effective_tool_name, tool_output);
        if (!sync_delegate_tool_output &&
            strcmp(effective_tool_name, "delegate_task") == 0 &&
            stats && stats->sync_delegate_completed) {
            sync_delegate_tool_output = strdup(tool_output);
            strscpy(sync_delegate_primary_tool_id, call->id, sizeof(sync_delegate_primary_tool_id));
        }

        append_tool_result_block(content, call->id, tool_output);
        kfree(rt.effective_input);
    }
    if (batch_executed && batch_output) {
        kfree(batch_output);
    }
    kfree(sync_delegate_tool_output);
    kfree(batch_coordinator_id);
    return content;
}
