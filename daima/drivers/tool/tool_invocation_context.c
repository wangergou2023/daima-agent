#include "drivers/tool/tool_invocation_context.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "drivers/channel/feishu/feishu_targets.h"
#include "cJSON.h"
#include "linux/printk.h"
static void json_set_string(cJSON *obj, const char *key, const char *value)
{
    if (!obj || !key || !value) {
        return;
    }
    cJSON_DeleteItemFromObject(obj, key);
    cJSON_AddStringToObject(obj, key, value);
}

static char *patch_cron_action_add_target(const llm_tool_call_t *call, const daima_msg_t *msg)
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

    if (channel && strcmp(channel, DAIMA_CHAN_FEISHU) == 0 && missing_chat_id) {
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

char *tool_invocation_context_patch_input(const llm_tool_call_t *call, const daima_msg_t *msg)
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
