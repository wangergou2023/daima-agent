#pragma once

#include "err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize Feishu credentials and internal state. */
daima_err_t feishu_bot_init(void);

/* Start Feishu WebSocket long-connection task (non-blocking). */
daima_err_t feishu_bot_start(void);

/* Send a Feishu chat message as a JSON 2.0 interactive card. */
daima_err_t feishu_send_card(const char *chat_id, const char *markdown);

/* Reply to a specific Feishu message as a JSON 2.0 interactive card. */
daima_err_t feishu_reply_card(const char *message_id, const char *markdown);

#ifdef __cplusplus
}
#endif
