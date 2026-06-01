#pragma once

#include <stdbool.h>

#include "channels/feishu/feishu_ws_client.h"

typedef struct {
    char url[512];
    int ping_interval_ms;
    int reconnect_interval_ms;
    int reconnect_nonce_ms;
    int service_id;
    bool connected;
} feishu_ws_runtime_t;

void feishu_ws_runtime_init(feishu_ws_runtime_t *rt);
void feishu_ws_runtime_run(feishu_ws_runtime_t *rt,
                           const char *app_id,
                           const char *app_secret);
