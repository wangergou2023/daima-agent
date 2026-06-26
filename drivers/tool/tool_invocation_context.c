/* 工具调用上下文补丁。
 * 在执行工具前，根据当前消息通道自动注入缺失的 channel/chat_id 参数。
 * 典型场景：cron add 时自动填充回复目标和飞书默认会话。 */

#include "drivers/tool/tool_invocation_context.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "drivers/channel/feishu/feishu_targets.h"
#include "cjson.h"
#include "linux/printk.h"

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

static bool message_is_delegate_subagent(const struct message *msg)
{
    if (!msg) {
        return false;
    }

    return strncmp(msg->chat_id, "delegate_sync_", 14) == 0;
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

    char *json = cJSON_PrintUnformatted(delegate);
    cJSON_Delete(delegate);
    return json;
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
    return NULL;
}

const char *tool_invocation_context_patch_tool_name(const llm_tool_call_t *call, const struct message *msg)
{
    if (!call || !msg) {
        return NULL;
    }
    if (strcmp(call->name, "files") != 0) {
        return NULL;
    }

    cJSON *root = cJSON_Parse(call->input ? call->input : "{}");
    bool should_delegate = files_call_looks_broad_discovery(call, root, msg);
    cJSON_Delete(root);
    return should_delegate ? "delegate_task" : NULL;
}
