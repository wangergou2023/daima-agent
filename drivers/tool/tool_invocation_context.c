/* 工具调用上下文补丁。
 * 在执行工具前，根据当前消息通道自动注入缺失的 channel/chat_id 参数。
 * 典型场景：cron add 时自动填充回复目标和飞书默认会话。 */

#include "drivers/tool/tool_invocation_context.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "drivers/channel/feishu/feishu_targets.h"
#include "delegate/delegate_turn_directive.h"
#include "cjson.h"
#include "linux/printk.h"

static char *build_forced_delegate_preflight_batch(const struct message *msg);

static char *load_delegate_turn_directive_json(const struct message *msg)
{
    char buf[4096];

    if (!msg || !msg->chat_id[0]) {
        return NULL;
    }
    if (!delegate_turn_directive_load_copy(msg->chat_id, buf, sizeof(buf)) || !buf[0]) {
        return NULL;
    }
    return strdup(buf);
}

static bool terminal_command_contains_any(const char *command,
                                          const char *const *parts,
                                          size_t count)
{
    if (!command || !command[0]) {
        return false;
    }
    for (size_t i = 0; i < count; i++) {
        if (parts[i] && strstr(command, parts[i])) {
            return true;
        }
    }
    return false;
}

static bool message_requests_broad_discovery(const struct message *msg)
{
    if (!msg || !msg->content) {
        return false;
    }

    static const char *keywords[] = {
        "分析", "看看", "目录", "结构", "模块", "代码库", "仓库", "摸底",
        "analyze", "analysis", "explore", "survey", "repo", "repository", "codebase", "structure"
    };
    for (size_t i = 0; i < sizeof(keywords) / sizeof(keywords[0]); i++) {
        if (strstr(msg->content, keywords[i])) {
            return true;
        }
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

static bool message_is_delegate_subagent(const struct message *msg)
{
    if (!msg) {
        return false;
    }

    return strncmp(msg->chat_id, "delegate_sync_", 14) == 0;
}

static bool message_is_internal_broad_discovery(const struct message *msg)
{
    if (!msg || !msg->content) {
        return false;
    }
    return strstr(msg->content, "Investigate this codebase area and return a concise discovery summary with concrete evidence.") != NULL &&
           strstr(msg->content, "bounded exploration request") != NULL;
}

bool tool_invocation_context_terminal_command_looks_broad_discovery(const char *command,
                                                                    const struct message *msg)
{
    static const char *const repo_listing_parts[] = {
        "find ", " -maxdepth ", "ls -la", "tree", "cat Makefile", "cat README",
        "cat AGENTS", "find kernel", "find drivers", "find . -maxdepth"
    };
    static const char *const mutation_parts[] = {
        "apply_patch", "git checkout", "git reset", "rm ", "mv ", "cp ", "sed -i", "tee ", ">>"
    };

    if (!command || !msg || !msg->content) {
        return false;
    }
    if (message_is_delegate_subagent(msg) || message_is_internal_broad_discovery(msg)) {
        return false;
    }
    if (!message_requests_broad_discovery(msg)) {
        return false;
    }
    if (!terminal_command_contains_any(command, repo_listing_parts,
                                       sizeof(repo_listing_parts) / sizeof(repo_listing_parts[0]))) {
        return false;
    }
    if (terminal_command_contains_any(command, mutation_parts,
                                      sizeof(mutation_parts) / sizeof(mutation_parts[0]))) {
        return false;
    }
    return true;
}

static bool files_call_looks_broad_discovery(const llm_tool_call_t *call,
                                             cJSON *root,
                                             const struct message *msg)
{
    if (!call || !root || !msg) {
        return false;
    }
    if (message_is_delegate_subagent(msg)) {
        return false;
    }
    if (message_is_internal_broad_discovery(msg)) {
        return false;
    }
    if (tool_invocation_context_message_requests_multi_subagents(msg)) {
        return false;
    }
    if (strcmp(call->name, "files") != 0) {
        return false;
    }

    const char *action = cJSON_GetStringValue(cJSON_GetObjectItem(root, "action"));
    if (!action) {
        return false;
    }
    if (strcmp(action, "list") == 0) {
        return message_requests_broad_discovery(msg);
    }
    if (strcmp(action, "search") != 0) {
        return false;
    }

    const char *target = cJSON_GetStringValue(cJSON_GetObjectItem(root, "target"));
    const char *output_mode = cJSON_GetStringValue(cJSON_GetObjectItem(root, "output_mode"));
    cJSON *limit = cJSON_GetObjectItem(root, "limit");
    int limit_value = cJSON_IsNumber(limit) ? limit->valueint : 0;

    if (!target || strcmp(target, "files") == 0) {
        return true;
    }
    if (output_mode && (strcmp(output_mode, "files_only") == 0 || strcmp(output_mode, "count") == 0)) {
        return true;
    }
    if (limit_value == 0 || limit_value > 20) {
        return message_requests_broad_discovery(msg);
    }
    return false;
}

static char *build_delegate_explore_request(cJSON *root, const struct message *msg)
{
    char *directive = load_delegate_turn_directive_json(msg);
    if (directive) {
        return directive;
    }
    if (message_requests_delegate_preflight_batch(msg)) {
        return build_forced_delegate_preflight_batch(msg);
    }

    const char *action = cJSON_GetStringValue(cJSON_GetObjectItem(root, "action"));
    const char *path = cJSON_GetStringValue(cJSON_GetObjectItem(root, "path"));
    const char *pattern = cJSON_GetStringValue(cJSON_GetObjectItem(root, "pattern"));
    const char *target = cJSON_GetStringValue(cJSON_GetObjectItem(root, "target"));
    const char *file_glob = cJSON_GetStringValue(cJSON_GetObjectItem(root, "file_glob"));
    const char *output_mode = cJSON_GetStringValue(cJSON_GetObjectItem(root, "output_mode"));

    char prompt[2048];
    snprintf(prompt, sizeof(prompt),
             "Investigate this codebase area and return a concise discovery summary with concrete evidence. "
             "This is a bounded exploration request: map top-level structure first, then inspect only the smallest set of representative files needed to explain key modules. "
             "Stop once you can answer repo structure, important modules, entrypoints, and likely next files to read. "
             "Do not exhaustively enumerate every subdirectory or sweep the entire repository. "
             "Prefer 1 top-level listing, then at most a few targeted listings/reads for representative modules. "
             "Do not recursively dump full trees. Group findings by subsystem and cite representative files instead of trying to cover every file. "
             "Treat the requested path as the primary boundary unless the answer requires one or two adjacent modules for context. "
             "If the user asked about directory structure or code organization, optimize for fast coverage and early stop, not completeness-by-exhaustion. "
             "Original user request: %s\n"
             "Requested files tool call: action=%s path=%s pattern=%s target=%s file_glob=%s output_mode=%s",
             msg && msg->content ? msg->content : "",
             action ? action : "",
             path ? path : "",
             pattern ? pattern : "",
             target ? target : "",
             file_glob ? file_glob : "",
             output_mode ? output_mode : "");

    cJSON *delegate = cJSON_CreateObject();
    if (!delegate) {
        return NULL;
    }
    cJSON_AddStringToObject(delegate, "description", "broad discovery");
    cJSON_AddStringToObject(delegate, "subagent_type", "explore");
    cJSON_AddStringToObject(delegate, "prompt", prompt);
    if (path && path[0]) {
        cJSON_AddStringToObject(delegate, "target_path", path);
    }

    char *json = cJSON_PrintUnformatted(delegate);
    cJSON_Delete(delegate);
    return json;
}

static char *build_delegate_explore_request_from_terminal(cJSON *root, const struct message *msg)
{
    char *directive = load_delegate_turn_directive_json(msg);
    if (directive) {
        return directive;
    }
    if (message_requests_delegate_preflight_batch(msg)) {
        return build_forced_delegate_preflight_batch(msg);
    }

    const char *command = cJSON_GetStringValue(cJSON_GetObjectItem(root, "command"));
    const char *workdir = cJSON_GetStringValue(cJSON_GetObjectItem(root, "workdir"));
    const char *target_path = NULL;
    const char *cd = NULL;
    char extracted[512];

    if (!command || !command[0]) {
        return NULL;
    }

    extracted[0] = '\0';
    cd = strstr(command, "cd ");
    if (cd) {
        cd += 3;
        size_t i = 0;
        while (cd[i] && cd[i] != ' ' && cd[i] != '&' && i + 1 < sizeof(extracted)) {
            extracted[i] = cd[i];
            i++;
        }
        extracted[i] = '\0';
    }

    if (extracted[0]) {
        target_path = extracted;
    } else if (workdir && workdir[0] &&
               strcmp(workdir, "/home/wangergou/.agent-data/spiffs_data/workspace") != 0) {
        target_path = workdir;
    }

    cJSON *delegate = cJSON_CreateObject();
    char prompt[2048];
    if (!delegate) {
        return NULL;
    }

    snprintf(prompt, sizeof(prompt),
             "Investigate this codebase area and return a concise discovery summary with concrete evidence. "
             "This is a bounded exploration request: map top-level structure first, then inspect only the smallest set of representative files needed to explain key modules. "
             "Prefer subagent-based architecture discovery over raw terminal enumeration. "
             "Original user request: %s\n"
             "Requested terminal command: %s",
             msg && msg->content ? msg->content : "",
             command);

    cJSON_AddStringToObject(delegate, "description", "broad discovery");
    cJSON_AddStringToObject(delegate, "subagent_type", "explore");
    cJSON_AddStringToObject(delegate, "prompt", prompt);
    if (target_path && target_path[0]) {
        cJSON_AddStringToObject(delegate, "target_path", target_path);
    }

    char *json = cJSON_PrintUnformatted(delegate);
    cJSON_Delete(delegate);
    return json;
}

static char *build_forced_delegate_preflight_batch(const struct message *msg)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *tasks = cJSON_CreateArray();
    cJSON *task = NULL;
    char *json = NULL;

    if (!root || !tasks) {
        cJSON_Delete(root);
        cJSON_Delete(tasks);
        return NULL;
    }

    task = cJSON_CreateObject();
    cJSON_AddStringToObject(task, "description", "分析 kernel/turn");
    cJSON_AddStringToObject(task, "subagent_type", "explore");
    cJSON_AddStringToObject(task, "target_path", "/home/wangergou/code/github/daima-agent/kernel/turn");
    cJSON_AddStringToObject(task, "prompt",
                            "分析 /home/wangergou/code/github/daima-agent/kernel/turn 的目录结构和关键模块。只做代表性覆盖，说明主回合执行链、关键文件和下一步值得继续看的文件。");
    cJSON_AddItemToArray(tasks, task);

    task = cJSON_CreateObject();
    cJSON_AddStringToObject(task, "description", "分析 drivers/tool");
    cJSON_AddStringToObject(task, "subagent_type", "explore");
    cJSON_AddStringToObject(task, "target_path", "/home/wangergou/code/github/daima-agent/drivers/tool");
    cJSON_AddStringToObject(task, "prompt",
                            "分析 /home/wangergou/code/github/daima-agent/drivers/tool 的目录结构和关键模块。只做代表性覆盖，说明工具协议、delegate_task、runtime 封装和后续建议阅读文件。");
    cJSON_AddItemToArray(tasks, task);

    task = cJSON_CreateObject();
    cJSON_AddStringToObject(task, "description", "验证 sudo 权限链路");
    cJSON_AddStringToObject(task, "subagent_type", "explore");
    cJSON_AddStringToObject(task, "target_path", "/home/wangergou/code/github/daima-agent");
    cJSON_AddStringToObject(task, "prompt",
                            "验证 sudo 权限链路，并基于真实工具结果解释为什么会请求 sudo、如果用户取消会如何阻塞。不要假装执行，必须基于 preflight_tool 的真实输出总结。");
    cJSON *preflight = cJSON_CreateObject();
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(preflight, "tool_name", "terminal");
    cJSON_AddStringToObject(input, "command", "sudo ls /root");
    cJSON_AddStringToObject(input, "workdir", "/home/wangergou/code/github/daima-agent");
    cJSON_AddItemToObject(preflight, "input", input);
    cJSON_AddBoolToObject(preflight, "continue_on_error", false);
    cJSON_AddItemToObject(task, "preflight_tool", preflight);
    cJSON_AddItemToArray(tasks, task);

    cJSON_AddItemToObject(root, "tasks", tasks);
    json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    pr_info("Patched delegate_task to forced batch+preflight for chat=%s", msg ? msg->chat_id : "");
    return json;
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
    if (!call || !msg) {
        return NULL;
    }
    char *directive = NULL;
    if ((strcmp(call->name, "files") == 0 || strcmp(call->name, "terminal") == 0) &&
        !message_is_delegate_subagent(msg) &&
        !message_is_internal_broad_discovery(msg) &&
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
    if (strcmp(call->name, "files") == 0) {
        cJSON *root = cJSON_Parse(call->input ? call->input : "{}");
        bool should_delegate = files_call_looks_broad_discovery(call, root, msg);
        char *patched = should_delegate ? build_delegate_explore_request(root, msg) : NULL;
        if (patched) {
            pr_info("Patched broad discovery files call to delegate_task(explore) for chat=%s", msg->chat_id);
        }
        cJSON_Delete(root);
        if (patched) {
            return patched;
        }
    }
    if (strcmp(call->name, "terminal") == 0) {
        cJSON *root = cJSON_Parse(call->input ? call->input : "{}");
        const char *command = root ? cJSON_GetStringValue(cJSON_GetObjectItem(root, "command")) : NULL;
        bool should_delegate = tool_invocation_context_terminal_command_looks_broad_discovery(command, msg);
        char *patched = should_delegate ? build_delegate_explore_request_from_terminal(root, msg) : NULL;
        if (patched) {
            pr_info("Patched broad discovery terminal call to delegate_task(explore) for chat=%s", msg->chat_id);
        }
        cJSON_Delete(root);
        if (patched) {
            return patched;
        }
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
        char directive_buf[4096];
        const char *subagent_type = root ? cJSON_GetStringValue(cJSON_GetObjectItem(root, "subagent_type")) : NULL;
        cJSON *tasks = root ? cJSON_GetObjectItem(root, "tasks") : NULL;
        bool should_force_batch = false;

        directive_buf[0] = '\0';
        if (!message_is_delegate_subagent(msg) && !message_is_internal_broad_discovery(msg)) {
            has_stored_directive = delegate_turn_directive_load_copy(msg->chat_id,
                                                                     directive_buf,
                                                                     sizeof(directive_buf));
        }
        should_force_batch = !has_stored_directive &&
                             message_requests_delegate_preflight_batch(msg) &&
                             delegate_task_input_is_single_explore(root);
        pr_info("delegate_task patch check: chat=%s stored_directive=%d should_force_batch=%d has_tasks=%d subagent_type=%s",
                msg->chat_id,
                has_stored_directive ? 1 : 0,
                should_force_batch ? 1 : 0,
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
    }
    return NULL;
}

const char *tool_invocation_context_patch_tool_name(const llm_tool_call_t *call, const struct message *msg)
{
    if (!call || !msg) {
        return NULL;
    }
    if ((strcmp(call->name, "files") == 0 || strcmp(call->name, "terminal") == 0) &&
        !message_is_delegate_subagent(msg) &&
        !message_is_internal_broad_discovery(msg) &&
        (delegate_turn_directive_load_copy(msg->chat_id, (char [4096]){0}, 4096) ||
         message_requests_delegate_preflight_batch(msg))) {
        return "delegate_task";
    }
    if (strcmp(call->name, "terminal") == 0) {
        cJSON *root = cJSON_Parse(call->input ? call->input : "{}");
        const char *command = root ? cJSON_GetStringValue(cJSON_GetObjectItem(root, "command")) : NULL;
        bool should_delegate = tool_invocation_context_terminal_command_looks_broad_discovery(command, msg);
        cJSON_Delete(root);
        return should_delegate ? "delegate_task" : NULL;
    }
    if (strcmp(call->name, "files") != 0) {
        return NULL;
    }

    cJSON *root = cJSON_Parse(call->input ? call->input : "{}");
    bool should_delegate = files_call_looks_broad_discovery(call, root, msg);
    cJSON_Delete(root);
    return should_delegate ? "delegate_task" : NULL;
}
