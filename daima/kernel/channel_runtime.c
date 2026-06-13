#include "channel_runtime.h"

#include <string.h>

#include "drivers/channel/feishu/feishu_bot.h"
#include "drivers/channel/vector/vector_channel.h"
#include "drivers/channel/gateway/ws_client.h"
#include "drivers/channel/gateway/ws_server.h"
#include "linux/printk.h"
#include "drivers/voice/voice_channel.h"
#include "drivers/voice/tts_player.h"
static daima_err_t channel_runtime_send_text(const char *channel,
                                             const char *chat_id,
                                             const char *text,
                                             const char *reasoning)
{
    if (!channel || !chat_id || !text) {
        return DAIMA_ERR_INVALID_ARG;
    }

    if (strcmp(channel, DAIMA_CHAN_WEBSOCKET) == 0) {
        return ws_server_send_with_reasoning(chat_id, text, reasoning);
    }
    if (strcmp(channel, DAIMA_CHAN_PET) == 0) {
        return ws_server_send_pet_response(chat_id, text);
    }
    if (strcmp(channel, DAIMA_CHAN_VOICE) == 0) {
        pr_info("Voice output: [%s]", text);
        return tts_player_speak(text);
    }
    if (strcmp(channel, DAIMA_CHAN_FEISHU) == 0) {
        return feishu_send_card(chat_id, text);
    }
    if (strcmp(channel, DAIMA_CHAN_SYSTEM) == 0) {
        pr_info("System message [%s]: %.128s", chat_id, text);
        return DAIMA_OK;
    }
    if (strcmp(channel, DAIMA_CHAN_VECTOR) == 0) {
        return vector_channel_send_reply(chat_id, text);
    }
    return DAIMA_ERR_INVALID_ARG;
}

daima_err_t channel_runtime_dispatch_outbound(const daima_msg_t *msg)
{
    if (!msg || !msg->content) {
        return DAIMA_ERR_INVALID_ARG;
    }
    daima_err_t err = channel_runtime_send_text(msg->channel, msg->chat_id, msg->content, msg->reasoning);
    if (err != DAIMA_OK && strcmp(msg->channel, DAIMA_CHAN_WEBSOCKET) == 0) {
        ws_pending_save(msg->content);
    }
    return err;
}
