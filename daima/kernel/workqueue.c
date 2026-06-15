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
static os_timer_t *s_heartbeat_timer = NULL;

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
    FILE *f = fopen(path_heartbeat_file(), "r");
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
        pr_debug("No actionable tasks in HEARTBEAT.md");
        return false;
    }

    struct message msg;
    char prompt[512];
    memset(&msg, 0, sizeof(msg));
    strncpy(msg.channel, CHAN_SYSTEM, sizeof(msg.channel) - 1);
    strncpy(msg.chat_id, "heartbeat", sizeof(msg.chat_id) - 1);
    strncpy(msg.source, MSG_SOURCE_HEARTBEAT, sizeof(msg.source) - 1);
    snprintf(prompt,
             sizeof(prompt),
             "Read %s and follow any instructions or tasks listed there. "
             "If nothing needs attention, reply with just: HEARTBEAT_OK",
             path_heartbeat_file());
    msg.content = strdup(prompt);

    if (!msg.content) {
        pr_err("Failed to allocate heartbeat prompt");
        return false;
    }

    err_t err = message_bus_push_inbound(&msg);
    if (err != 0) {
        pr_warn("Failed to push heartbeat message: %s", err_name(err));
        kfree(msg.content);
        return false;
    }

    pr_info("Triggered agent check");
    return true;
}

/* ── 定时器回调 ───────────────────────────────────────────── */

static void heartbeat_timer_callback(os_timer_t *timer)
{
    (void)timer;
    heartbeat_send();
}

/* ── 对外接口 ───────────────────────────────────────────────── */

err_t heartbeat_init(void)
{
    pr_info("Heartbeat service initialized (file: %s, interval: %ds)", path_heartbeat_file(), heartbeat_interval_ms() / 1000);
    return 0;
}

err_t heartbeat_start(void)
{
    if (s_heartbeat_timer) {
        pr_warn("Heartbeat timer already running");
        return 0;
    }

    s_heartbeat_timer = os_timer_create(
        "heartbeat",
        heartbeat_interval_ms(),
        true,    /* 自动重载 */
        NULL,
        heartbeat_timer_callback
    );

    if (!s_heartbeat_timer) {
        pr_err("Failed to create heartbeat timer");
        return ERR_FAIL;
    }

    if (!os_timer_start(s_heartbeat_timer, 1000)) {
        pr_err("Failed to start heartbeat timer");
        return ERR_FAIL;
    }

    pr_info("Heartbeat started (every %d min)", heartbeat_interval_ms() / 60000);
    return 0;
}

void heartbeat_stop(void)
{
    if (s_heartbeat_timer) {
        os_timer_stop(s_heartbeat_timer, 1000);
        os_timer_delete(s_heartbeat_timer, 1000);
        s_heartbeat_timer = NULL;
        pr_info("Heartbeat stopped");
    }
}

bool heartbeat_trigger(void)
{
    return heartbeat_send();
}
