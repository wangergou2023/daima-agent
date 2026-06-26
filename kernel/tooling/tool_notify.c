/* 工具活动通知：格式化工具调用事件为人类可读文本并发送到各通道。
 * 对不同通道使用不同通知策略（websocket 实时、飞书仅发重点）。 */

#include "tool_notify.h"

#include <stdio.h>
#include <string.h>

#include "drivers/channel/feishu/feishu_bot.h"
#include "cjson.h"
#include "linux/printk.h"
#include "drivers/channel/gateway/ws_server.h"
#define FEISHU_TOOL_ACTIVITY_SLOW_MS 1500

static bool websocket_chat_is_delegate_subagent(const struct message *msg)
{
    if (!msg) {
        return false;
    }
    return strncmp(msg->chat_id, "delegate_sync_", 14) == 0;
}

/** 工具名 → emoji 图标映射。 */
static const char *tool_display_icon(const char *tool_name)
{
    if (!tool_name) return "⚙";
    if (strcmp(tool_name, "terminal") == 0) return "💻";
    if (strcmp(tool_name, "files") == 0) return "📖";
    if (strcmp(tool_name, "apply_patch") == 0) return "✏️";
    if (strcmp(tool_name, "weather") == 0) return "🌤️";
    if (strcmp(tool_name, "get_current_time") == 0) return "🕒";
    if (strcmp(tool_name, "cron") == 0) return "⏰";
    if (strcmp(tool_name, "skills") == 0) return "🧩";
    return "⚙";
}

static bool str_contains_any(const char *s, const char *const *needles, size_t count)
{
    if (!s) return false;
    for (size_t i = 0; i < count; i++) {
        if (needles[i] && strstr(s, needles[i])) {
            return true;
        }
    }
    return false;
}

static const char *terminal_command_from_input(const char *tool_input, cJSON **root_out)
{
    if (root_out) {
        *root_out = NULL;
    }
    cJSON *root = cJSON_Parse(tool_input ? tool_input : "{}");
    if (!root) {
        return NULL;
    }
    const char *command = cJSON_GetStringValue(cJSON_GetObjectItem(root, "command"));
    if (!command || !command[0]) {
        command = cJSON_GetStringValue(cJSON_GetObjectItem(root, "cmd"));
    }
    if (root_out) {
        *root_out = root;
    } else {
        cJSON_Delete(root);
    }
    return command;
}

static bool terminal_command_is_noteworthy(const char *command)
{
    static const char *const keywords[] = {
        "sudo",
        "apt ",
        "apt-get",
        "yum ",
        "dnf ",
        "pip install",
        "npm install",
        "pnpm install",
        "cmake --build",
        "make ",
        "ninja ",
        "git ",
    };
    return str_contains_any(command, keywords, sizeof(keywords) / sizeof(keywords[0]));
}

/** 飞书通道是否应发送工具活动通知：失败总是发，terminal 慢(>1.5s)或敏感命令发。 */
static bool tool_activity_should_send_feishu(const tool_activity_event_t *event)
{
    if (!event || !event->tool_name) {
        return false;
    }
    if (!event->ok) {
        return true;
    }
    if (strcmp(event->tool_name, "cron") == 0) {
        return true;
    }
    if (strcmp(event->tool_name, "terminal") == 0) {
        if (event->elapsed_ms >= FEISHU_TOOL_ACTIVITY_SLOW_MS) {
            return true;
        }
        cJSON *root = NULL;
        const char *command = terminal_command_from_input(event->tool_input, &root);
        bool noteworthy = terminal_command_is_noteworthy(command);
        cJSON_Delete(root);
        return noteworthy;
    }
    return false;
}

/** 格式化飞书工具活动行为单行文本（含图标、名称、目标、耗时/失败原因）。 */
static void format_feishu_tool_activity_line(const tool_activity_event_t *event,
                                             char *buf,
                                             size_t size)
{
    if (!buf || size == 0) {
        return;
    }
    buf[0] = '\0';
    if (!event) {
        return;
    }

    const char *icon = tool_display_icon(event->tool_name);
    const char *name = event->tool_name ? event->tool_name : "tool";
    double seconds = (double)event->elapsed_ms / 1000.0;

    if (!event->ok) {
        if (event->target && event->target[0]) {
            snprintf(buf, size, "%s %s：%s失败%s%s",
                     icon, name, event->target,
                     event->detail && event->detail[0] ? "（" : "",
                     event->detail && event->detail[0] ? event->detail : "");
        } else {
            snprintf(buf, size, "%s %s失败%s%s",
                     icon, name,
                     event->detail && event->detail[0] ? "（" : "",
                     event->detail && event->detail[0] ? event->detail : "");
        }
        if (event->detail && event->detail[0]) {
            size_t len = strlen(buf);
            if (len + 4 < size) {
                snprintf(buf + len, size - len, "）");
            }
        }
        return;
    }

    if (event->tool_name && strcmp(event->tool_name, "cron") == 0) {
        cJSON *root = cJSON_Parse(event->tool_input ? event->tool_input : "{}");
        const char *action = root ? cJSON_GetStringValue(cJSON_GetObjectItem(root, "action")) : NULL;
        bool is_add = action && strcmp(action, "add") == 0;
        cJSON_Delete(root);
        if (!is_add) {
            goto generic_success;
        }
        if (event->target && event->target[0]) {
            snprintf(buf, size, "%s 已设置提醒：%s", icon, event->target);
        } else {
            snprintf(buf, size, "%s 已设置提醒", icon);
        }
        return;
    }

generic_success:
    if (event->target && event->target[0]) {
        snprintf(buf, size, "%s %s：%s（%.1fs）", icon, name, event->target, seconds);
    } else {
        snprintf(buf, size, "%s %s（%.1fs）", icon, name, seconds);
    }
}

/** 分发工具活动通知到各通道：websocket 发送 tool_event，飞书发送格式化卡片行。 */
err_t channel_runtime_send_tool_activity(const struct message *msg,
                                              const tool_activity_event_t *event)
{
    if (!msg || !event || !event->default_text) {
        return ERR_INVALID_ARG;
    }

    if (strcmp(msg->channel, CHAN_WEBSOCKET) == 0) {
        if (websocket_chat_is_delegate_subagent(msg)) {
            return 0;
        }
        return ws_server_send_tool_event(msg->chat_id, event->default_text);
    }
    if (strcmp(msg->channel, CHAN_FEISHU) == 0) {
        if (!tool_activity_should_send_feishu(event)) {
            return 0;
        }
        char feishu_line[256];
        format_feishu_tool_activity_line(event, feishu_line, sizeof(feishu_line));
        return feishu_send_card(msg->chat_id, feishu_line[0] ? feishu_line : event->default_text);
    }
    return 0;
}
