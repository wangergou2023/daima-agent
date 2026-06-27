/* 通道运行时调度：将消息按 channel 类型分发到对应的通道驱动。
 * channel_runtime_send_text() 是核心路由表，channel_runtime_dispatch_outbound() 是上层封装。 */

#include "channel_runtime.h"

#include <string.h>

#include "drivers/channel/feishu/feishu_bot.h"
#include "drivers/channel/vector/vector_channel.h"
#include "drivers/channel/gateway/ws_client.h"
#include "drivers/channel/gateway/ws_server.h"
#include "turn_common.h"
#include "linux/printk.h"
#include "drivers/voice/voice_channel.h"
#include "drivers/voice/tts_player.h"
#include "delegate/delegate_task_store.h"
#include "delegate/delegate_parent_wake.h"

static channel_runtime_sender_fn_t s_sender_override_for_test;
/** 按通道类型文本发送：websocket → ws_server，pet → ws_server_pet，voice → tts_speak，feishu → feishu_send_card。 */
static err_t channel_runtime_send_text(const char *channel,
                                             const char *chat_id,
                                             const char *text,
                                             const char *reasoning)
{
    if (!channel || !chat_id || !text) {
        return ERR_INVALID_ARG;
    }
    if (s_sender_override_for_test) {
        return s_sender_override_for_test(channel, chat_id, text, reasoning);
    }

    if (strcmp(channel, CHAN_WEBSOCKET) == 0) {
        return ws_server_send_with_reasoning(chat_id, text, reasoning);
    }
    if (strcmp(channel, CHAN_PET) == 0) {
        return ws_server_send_pet_response(chat_id, text);
    }
    if (strcmp(channel, CHAN_VOICE) == 0) {
        pr_info("Voice output: [%s]", text);
        return tts_player_speak(text);
    }
    if (strcmp(channel, CHAN_FEISHU) == 0) {
        return feishu_send_card(chat_id, text);
    }
    if (strcmp(channel, CHAN_SYSTEM) == 0) {
        pr_info("System message [%s]: %.128s", chat_id, text);
        return 0;
    }
    if (strcmp(channel, CHAN_VECTOR) == 0) {
        return vector_channel_send_reply(chat_id, text);
    }
    return ERR_INVALID_ARG;
}

/** 出站消息分发：调用 channel_runtime_send_text，websocket 发送失败时保存待重发。 */
err_t channel_runtime_dispatch_outbound(const struct message *msg)
{
    if (!msg || !msg->content) {
        return ERR_INVALID_ARG;
    }
    err_t err = channel_runtime_send_text(msg->channel, msg->chat_id, msg->content, msg->reasoning);
    if (err == 0 &&
        strcmp(msg->channel, CHAN_WEBSOCKET) == 0 &&
        strcmp(agent_msg_source_or_default(msg), MSG_SOURCE_DELEGATE) != 0) {
        delegate_task_store_mark_parent_response_sent(msg->chat_id);
        delegate_parent_wake_record_parent_activity(msg->chat_id);
    }
    if (err != 0 && strcmp(msg->channel, CHAN_WEBSOCKET) == 0) {
        ws_pending_save(msg->content);
    }
    return err;
}

void channel_runtime_set_sender_override_for_test(channel_runtime_sender_fn_t sender)
{
    s_sender_override_for_test = sender;
}
