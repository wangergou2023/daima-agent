#include "turn_persist.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "turn_common.h"
#include "context_compress.h"
#include "learning.h"
#include "runtime.h"
#include "bus.h"
#include "drivers/memory/session_store.h"
#include "linux/printk.h"
#include "cJSON.h"
#include "linux/slab.h"
static bool should_save_assistant_reply(const struct message *msg, const char *final_text)
{
    return msg && final_text && final_text[0] && !agent_msg_is_internal_control(msg);
}

static char *build_assistant_session_content_json(const char *text, const char *reasoning)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return NULL;
    }
    cJSON_AddStringToObject(root, "text", text ? text : "");
    if (reasoning && reasoning[0]) {
        cJSON_AddStringToObject(root, "reasoning", reasoning);
    }
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

void agent_turn_queue_outbound_text(const struct message *msg, char *text, const char *reasoning, bool free_on_fail)
{
    if (!msg || !text) {
        kfree(text);
        return;
    }
    if (agent_msg_is_internal_control(msg)) {
        pr_info("Skip outbound response for internal control message");
        kfree(text);
        return;
    }

    struct message out = {0};
    strncpy(out.channel, msg->channel, sizeof(out.channel) - 1);
    strncpy(out.chat_id, msg->chat_id, sizeof(out.chat_id) - 1);
    out.content = text;
    out.reasoning = reasoning && reasoning[0] ? strdup(reasoning) : NULL;

    pr_info("Queue final response to %s:%s (%d bytes)", out.channel, out.chat_id, (int)strlen(text));
    if (message_bus_push_outbound(&out) != 0) {
        pr_warn("Outbound queue full, drop response");
        if (free_on_fail) {
            kfree(text);
        }
        kfree(out.reasoning);
    }
}

void agent_turn_save_session(const struct message *msg, const char *final_text, const char *reasoning, int iteration)
{
    if (!msg || !msg->chat_id[0] || !final_text || !final_text[0]) {
        return;
    }

    err_t save_inbound = 0;
    err_t save_asst = 0;
    bool saved_any = false;
    const char *inbound_role = agent_session_role_for_inbound_msg(msg);

    if (inbound_role) {
        const char *inbound_text = msg->content ? msg->content : "";
        save_inbound = session_store_append_ex(
            msg->chat_id,
            inbound_role,
            inbound_text,
            agent_msg_source_or_default(msg));
        if (save_inbound == 0) {
            saved_any = true;
        }
    }

    if (should_save_assistant_reply(msg, final_text)) {
        char *assistant_payload = build_assistant_session_content_json(final_text, reasoning);
        if (assistant_payload) {
            save_asst = session_store_append(msg->chat_id, "assistant", assistant_payload);
            kfree(assistant_payload);
        } else {
            save_asst = ERR_NO_MEM;
        }
        if (save_asst == 0) {
            saved_any = true;
        }
    }

    if (save_inbound != 0 || save_asst != 0) {
        pr_warn("Session save failed for chat %s (source=%s, inbound=%s, assistant=%s)", msg->chat_id, agent_msg_source_or_default(msg), err_name(save_inbound), err_name(save_asst));
        return;
    }

    if (!saved_any) {
        pr_info("Skip session save for chat %s (source=%s)", msg->chat_id, agent_msg_source_or_default(msg));
        return;
    }

    pr_info("Session saved for chat %s (source=%s)", msg->chat_id, agent_msg_source_or_default(msg));
    context_compressor_schedule_if_needed(msg->chat_id);
    if (iteration >= 1 && runtime_config_get_learning_review_enabled()) {
        learning_review_schedule(msg->chat_id);
    }
}

char *agent_turn_build_error_reply(bool tool_budget_exhausted)
{
    if (tool_budget_exhausted) {
        return strdup("我已经把可用工具轮次用完了，但还没来得及整理出最终结果。请让我减少探索步骤后再试一次，或把目标范围再收窄一点。");
    }
    return strdup("Sorry, I encountered an error.");
}
