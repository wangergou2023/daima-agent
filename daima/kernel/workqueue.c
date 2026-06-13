/* 心跳/定时触发逻辑。 */

#include "linux/workqueue.h"
#include "runtime.h"
#include "paths.h"
#include "autoconf.h"
#include "bus.h"
#include "os.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include "linux/printk.h"
#include "linux/slab.h"

static const char *TAG = "heartbeat";

static daima_timer_t *s_heartbeat_timer = NULL;

static int heartbeat_interval_ms(void)
{
    return runtime_config_get_heartbeat_interval_ms();
}

/* ── 内容检查 ────────────────────────────────────────────── */

/**
 * 检查 HEARTBEAT.md 是否包含可执行内容。
 * 若存在以下条件之外的任一行，则返回 true：
 *   - 空行 / 仅空白
 *   - Markdown 标题（以 # 开头）
 *   - 已完成的复选项（- [x] 或 * [x]）
 */
static bool heartbeat_has_tasks(void)
{
    FILE *f = fopen(daima_path_heartbeat_file(), "r");
    if (!f) {
        return false;
    }

    char line[256];
    bool found_task = false;

    while (fgets(line, sizeof(line), f)) {
        /* 跳过行首空白 */
        const char *p = line;
        while (*p && isspace((unsigned char)*p)) {
            p++;
        }

        /* 跳过空行 */
        if (*p == '\0') {
            continue;
        }

        /* 跳过 Markdown 标题 */
        if (*p == '#') {
            continue;
        }

        /* 跳过已完成复选项："- [x]" 或 "* [x]" */
        if ((*p == '-' || *p == '*') && *(p + 1) == ' ' && *(p + 2) == '[') {
            char mark = *(p + 3);
            if ((mark == 'x' || mark == 'X') && *(p + 4) == ']') {
                continue;
            }
        }

        /* 找到可执行的行 */
        found_task = true;
        break;
    }

    fclose(f);
    return found_task;
}

/* ── 向智能体发送心跳 ──────────────────────────────────── */

static bool heartbeat_send(void)
{
    if (!heartbeat_has_tasks()) {
        DAIMA_LOGD(TAG, "No actionable tasks in HEARTBEAT.md");
        return false;
    }

    daima_msg_t msg;
    char prompt[512];
    memset(&msg, 0, sizeof(msg));
    strncpy(msg.channel, DAIMA_CHAN_SYSTEM, sizeof(msg.channel) - 1);
    strncpy(msg.chat_id, "heartbeat", sizeof(msg.chat_id) - 1);
    strncpy(msg.source, DAIMA_MSG_SOURCE_HEARTBEAT, sizeof(msg.source) - 1);
    snprintf(prompt,
             sizeof(prompt),
             "Read %s and follow any instructions or tasks listed there. "
             "If nothing needs attention, reply with just: HEARTBEAT_OK",
             daima_path_heartbeat_file());
    msg.content = strdup(prompt);

    if (!msg.content) {
        DAIMA_LOGE(TAG, "Failed to allocate heartbeat prompt");
        return false;
    }

    daima_err_t err = message_bus_push_inbound(&msg);
    if (err != DAIMA_OK) {
        DAIMA_LOGW(TAG, "Failed to push heartbeat message: %s", daima_err_to_name(err));
        kfree(msg.content);
        return false;
    }

    DAIMA_LOGI(TAG, "Triggered agent check");
    return true;
}

/* ── 定时器回调 ───────────────────────────────────────────── */

static void heartbeat_timer_callback(daima_timer_t *timer)
{
    (void)timer;
    heartbeat_send();
}

/* ── 对外接口 ───────────────────────────────────────────────── */

daima_err_t heartbeat_init(void)
{
    DAIMA_LOGI(TAG, "Heartbeat service initialized (file: %s, interval: %ds)",
             daima_path_heartbeat_file(), heartbeat_interval_ms() / 1000);
    return DAIMA_OK;
}

daima_err_t heartbeat_start(void)
{
    if (s_heartbeat_timer) {
        DAIMA_LOGW(TAG, "Heartbeat timer already running");
        return DAIMA_OK;
    }

    s_heartbeat_timer = daima_timer_create(
        "heartbeat",
        heartbeat_interval_ms(),
        true,    /* 自动重载 */
        NULL,
        heartbeat_timer_callback
    );

    if (!s_heartbeat_timer) {
        DAIMA_LOGE(TAG, "Failed to create heartbeat timer");
        return DAIMA_FAIL;
    }

    if (!daima_timer_start(s_heartbeat_timer, 1000)) {
        DAIMA_LOGE(TAG, "Failed to start heartbeat timer");
        return DAIMA_FAIL;
    }

    DAIMA_LOGI(TAG, "Heartbeat started (every %d min)", heartbeat_interval_ms() / 60000);
    return DAIMA_OK;
}

void heartbeat_stop(void)
{
    if (s_heartbeat_timer) {
        daima_timer_stop(s_heartbeat_timer, 1000);
        daima_timer_delete(s_heartbeat_timer, 1000);
        s_heartbeat_timer = NULL;
        DAIMA_LOGI(TAG, "Heartbeat stopped");
    }
}

bool heartbeat_trigger(void)
{
    return heartbeat_send();
}
