/* Turn 持久化：保存会话历史、排队出站消息、生成错误回复。
 * turn_finish 阶段调用，将本轮 assistant 回复和用户消息写入 session_store。 */

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
#include "cjson.h"
#include "linux/slab.h"
#include "turn_dispatch.h"

/** 判断是否应将助手回复保存到会话历史（非内部消息且有内容）。 */
static bool should_save_assistant_reply(const struct message *msg, const char *final_text)
{
    return msg && final_text && final_text[0] && !agent_msg_is_internal_control(msg);
}

/** 构建助手消息的 JSON 载荷（text + 可选 reasoning）。调用方负责释放。 */
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

/** 将回复文本放入出站总线队列，由通道路由异步分发。
 *  @param free_on_fail  入队失败时是否释放 text（传 true 时调用方不持有所有权） */
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

/** 保存本轮会话：用户消息 + assistant 回复 → session_store，并触发压缩和复盘。 */
void agent_turn_save_session(const struct message *msg, const char *final_text, const char *reasoning, int iteration)
{
    if (!msg || !msg->chat_id[0] || !final_text || !final_text[0]) {
        return;
    }

    const char *inbound_role = agent_session_role_for_inbound_msg(msg);

    if (inbound_role) {
        dispatch_save_session_sourced(msg->chat_id, inbound_role,
                                       msg->content ? msg->content : "",
                                       agent_msg_source_or_default(msg));
    }

    if (should_save_assistant_reply(msg, final_text)) {
        char *payload = build_assistant_session_content_json(final_text, reasoning);
        if (payload) {
            dispatch_save_session(msg->chat_id, "assistant", payload);
            kfree(payload);
        }
    }

    pr_info("Session saved for chat %s (source=%s)", msg->chat_id, agent_msg_source_or_default(msg));
    context_compressor_schedule_if_needed(msg->chat_id);
    if (iteration >= 1 && runtime_config_get_learning_review_enabled()) {
        learning_review_schedule(msg->chat_id);
    }
}

/** 生成错误回复文本（工具预算耗尽时的中文提示或通用英文报错）。 */
char *agent_turn_build_error_reply(bool tool_budget_exhausted)
{
    if (tool_budget_exhausted) {
        return strdup("我已经把可用工具轮次用完了，但还没来得及整理出最终结果。请让我减少探索步骤后再试一次，或把目标范围再收窄一点。");
    }
    return strdup("Sorry, I encountered an error.");
}
