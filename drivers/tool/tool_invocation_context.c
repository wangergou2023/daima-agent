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
