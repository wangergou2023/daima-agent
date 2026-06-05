#include "app/tool_activity_notifier.h"

#include <stdio.h>
#include <string.h>

#include "channels/feishu/feishu_bot.h"
#include "cJSON.h"
#include "daima_log.h"
#include "gateway/ws_server.h"

static const char *TAG = "tool_activity_notifier";
#define FEISHU_TOOL_ACTIVITY_SLOW_MS 1500

static const char *tool_display_icon(const char *tool_name)
{
    if (!tool_name) return "⚙";
    if (strcmp(tool_name, "terminal") == 0) return "💻";
    if (strcmp(tool_name, "read_file") == 0) return "📖";
    if (strcmp(tool_name, "write_file") == 0 || strcmp(tool_name, "edit_file") == 0) return "✏️";
    if (strcmp(tool_name, "list_dir") == 0) return "🗂️";
    if (strcmp(tool_name, "weather") == 0) return "🌤️";
    if (strcmp(tool_name, "get_current_time") == 0) return "🕒";
    if (strcmp(tool_name, "cron_add") == 0 || strcmp(tool_name, "cron_list") == 0 || strcmp(tool_name, "cron_remove") == 0) return "⏰";
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

static bool tool_activity_should_send_feishu(const daima_tool_activity_event_t *event)
{
    if (!event || !event->tool_name) {
        return false;
    }
    if (!event->ok) {
        return true;
    }
    if (strcmp(event->tool_name, "cron_add") == 0 ||
        strcmp(event->tool_name, "cron_list") == 0 ||
        strcmp(event->tool_name, "cron_remove") == 0) {
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

static void format_feishu_tool_activity_line(const daima_tool_activity_event_t *event,
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

    if (event->tool_name && strcmp(event->tool_name, "cron_add") == 0) {
        if (event->target && event->target[0]) {
            snprintf(buf, size, "%s 已设置提醒：%s", icon, event->target);
        } else {
            snprintf(buf, size, "%s 已设置提醒", icon);
        }
        return;
    }

    if (event->target && event->target[0]) {
        snprintf(buf, size, "%s %s：%s（%.1fs）", icon, name, event->target, seconds);
    } else {
        snprintf(buf, size, "%s %s（%.1fs）", icon, name, seconds);
    }
}

daima_err_t channel_runtime_send_tool_activity(const daima_msg_t *msg,
                                              const daima_tool_activity_event_t *event)
{
    if (!msg || !event || !event->default_text) {
        return DAIMA_ERR_INVALID_ARG;
    }

    if (strcmp(msg->channel, DAIMA_CHAN_WEBSOCKET) == 0) {
        return ws_server_send_tool_event(msg->chat_id, event->default_text);
    }
    if (strcmp(msg->channel, DAIMA_CHAN_FEISHU) == 0) {
        if (!tool_activity_should_send_feishu(event)) {
            return DAIMA_OK;
        }
        char feishu_line[256];
        format_feishu_tool_activity_line(event, feishu_line, sizeof(feishu_line));
        return feishu_send_card(msg->chat_id, feishu_line[0] ? feishu_line : event->default_text);
    }
    return DAIMA_OK;
}
