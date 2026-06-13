#include "drivers/channel/feishu/feishu_bot.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "drivers/channel/feishu/feishu_api.h"
#include "drivers/channel/feishu/feishu_ws_runtime.h"
#include "core/runtime.h"
#include "core/config.h"
#include "core/log.h"
#include "core/os.h"
#include "core/text.h"
#include "core/proxy.h"

static const char *TAG = "feishu";

static char s_app_id[64] = {0};
static char s_app_secret[128] = {0};
static daima_task_t *s_ws_task = NULL;
static feishu_ws_runtime_t s_ws_runtime;

static void feishu_ws_task(void *arg)
{
    (void)arg;
    feishu_ws_runtime_run(&s_ws_runtime, s_app_id, s_app_secret);
}

daima_err_t feishu_bot_init(void)
{
    const char *app_id = runtime_config_get_feishu_app_id();
    const char *app_secret = runtime_config_get_feishu_app_secret();

    feishu_ws_runtime_init(&s_ws_runtime);

    if (app_id && app_id[0]) {
        daima_safe_copy(s_app_id, sizeof(s_app_id), app_id);
    }
    if (app_secret && app_secret[0]) {
        daima_safe_copy(s_app_secret, sizeof(s_app_secret), app_secret);
    }

    if (s_app_id[0] && s_app_secret[0]) {
        DAIMA_LOGI(TAG, "Feishu credentials loaded (app_id=%.8s...)", s_app_id);
    } else {
        DAIMA_LOGW(TAG, "No Feishu credentials configured in config.json");
    }

    srand((unsigned int)time(NULL));
    return DAIMA_OK;
}

daima_err_t feishu_bot_start(void)
{
    if (s_app_id[0] == '\0' || s_app_secret[0] == '\0') {
        DAIMA_LOGW(TAG, "Feishu not configured, skipping WebSocket start");
        return DAIMA_OK;
    }
    if (http_proxy_is_enabled()) {
        DAIMA_LOGW(TAG, "Feishu WS ignores proxy settings in host mode");
    }
    if (s_ws_task) {
        DAIMA_LOGW(TAG, "Feishu WebSocket task already running");
        return DAIMA_OK;
    }
    bool ok = daima_task_create(
        feishu_ws_task,
        "feishu_ws",
        DAIMA_FEISHU_POLL_STACK,
        NULL,
        DAIMA_FEISHU_POLL_PRIO,
        &s_ws_task);
    if (!ok) {
        s_ws_task = NULL;
        return DAIMA_FAIL;
    }
    DAIMA_LOGI(TAG, "Feishu WebSocket mode enabled");
    return DAIMA_OK;
}

daima_err_t feishu_send_card(const char *chat_id, const char *markdown)
{
    if (!chat_id || !markdown) return DAIMA_ERR_INVALID_ARG;
    if (s_app_id[0] == '\0' || s_app_secret[0] == '\0') {
        DAIMA_LOGW(TAG, "Cannot send: no credentials configured");
        return DAIMA_ERR_INVALID_STATE;
    }
    return feishu_api_send_card(s_app_id, s_app_secret, chat_id, markdown);
}

daima_err_t feishu_reply_card(const char *message_id, const char *markdown)
{
    if (!message_id || !markdown) return DAIMA_ERR_INVALID_ARG;
    if (s_app_id[0] == '\0' || s_app_secret[0] == '\0') {
        return DAIMA_ERR_INVALID_STATE;
    }
    return feishu_api_reply_card(s_app_id, s_app_secret, message_id, markdown);
}
