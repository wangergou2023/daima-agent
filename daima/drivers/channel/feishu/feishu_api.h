/* 飞书 OpenAPI / WS 配置辅助。 */

#pragma once

#include <stddef.h>

#include "core/err.h"

typedef struct {
    char url[512];
    int service_id;
    int ping_interval_ms;
    int reconnect_interval_ms;
    int reconnect_nonce_ms;
} feishu_ws_config_t;

void feishu_api_reset_token_cache(void);

daima_err_t feishu_api_get_tenant_token(const char *app_id,
                                       const char *app_secret,
                                       char *token,
                                       size_t token_size);

daima_err_t feishu_api_pull_ws_config(const char *app_id,
                                     const char *app_secret,
                                     feishu_ws_config_t *out);

daima_err_t feishu_api_send_card(const char *app_id,
                                 const char *app_secret,
                                 const char *chat_id,
                                 const char *markdown);

daima_err_t feishu_api_reply_card(const char *app_id,
                                  const char *app_secret,
                                  const char *message_id,
                                  const char *markdown);
