#include "agent/agent_turn_persist.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "agent/agent_turn_common.h"
#include "agent/context_compressor.h"
#include "agent/learning_review.h"
#include "app/runtime_config.h"
#include "bus/message_bus.h"
#include "memory/session_store.h"
#include "daima_log.h"

static const char *TAG = "agent_finish";

static bool should_save_assistant_reply(const daima_msg_t *msg, const char *final_text)
{
    return msg && final_text && final_text[0] && !agent_msg_is_internal_control(msg);
}

void agent_turn_queue_outbound_text(const daima_msg_t *msg, char *text, bool free_on_fail)
{
    if (!msg || !text) {
        free(text);
        return;
    }
    if (agent_msg_is_internal_control(msg)) {
        DAIMA_LOGI(TAG, "Skip outbound response for internal control message");
        free(text);
        return;
    }

    daima_msg_t out = {0};
    strncpy(out.channel, msg->channel, sizeof(out.channel) - 1);
    strncpy(out.chat_id, msg->chat_id, sizeof(out.chat_id) - 1);
    out.content = text;

    DAIMA_LOGI(TAG, "Queue final response to %s:%s (%d bytes)",
              out.channel, out.chat_id, (int)strlen(text));
    if (message_bus_push_outbound(&out) != DAIMA_OK) {
        DAIMA_LOGW(TAG, "Outbound queue full, drop response");
        if (free_on_fail) {
            free(text);
        }
    }
}

void agent_turn_save_session(const daima_msg_t *msg, const char *final_text, int iteration)
{
    if (!msg || !msg->chat_id[0] || !final_text || !final_text[0]) {
        return;
    }

    daima_err_t save_inbound = DAIMA_OK;
    daima_err_t save_asst = DAIMA_OK;
    bool saved_any = false;
    const char *inbound_role = agent_session_role_for_inbound_msg(msg);

    if (inbound_role) {
        const char *inbound_text = msg->content ? msg->content : "";
        save_inbound = session_store_append_ex(
            msg->chat_id,
            inbound_role,
            inbound_text,
            agent_msg_source_or_default(msg));
        if (save_inbound == DAIMA_OK) {
            saved_any = true;
        }
    }

    if (should_save_assistant_reply(msg, final_text)) {
        save_asst = session_store_append(msg->chat_id, "assistant", final_text);
        if (save_asst == DAIMA_OK) {
            saved_any = true;
        }
    }

    if (save_inbound != DAIMA_OK || save_asst != DAIMA_OK) {
        DAIMA_LOGW(TAG, "Session save failed for chat %s (source=%s, inbound=%s, assistant=%s)",
                  msg->chat_id,
                  agent_msg_source_or_default(msg),
                  daima_err_to_name(save_inbound),
                  daima_err_to_name(save_asst));
        return;
    }

    if (!saved_any) {
        DAIMA_LOGI(TAG, "Skip session save for chat %s (source=%s)",
                  msg->chat_id, agent_msg_source_or_default(msg));
        return;
    }

    DAIMA_LOGI(TAG, "Session saved for chat %s (source=%s)",
              msg->chat_id, agent_msg_source_or_default(msg));
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
