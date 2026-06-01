#include "app/channel_runtime.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "bus/message_bus.h"
#include "channels/feishu/feishu_bot.h"
#include "channels/vector/vector_channel.h"
#include "gateway/ws_server.h"
#include "daima_log.h"
#include "voice/voice_channel.h"
#include "voice/tts_player.h"
#include "cJSON.h"

static const char *TAG = "channel_runtime";
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

static daima_err_t channel_runtime_send_text(const char *channel, const char *chat_id, const char *text)
{
    if (!channel || !chat_id || !text) {
        return DAIMA_ERR_INVALID_ARG;
    }

    if (strcmp(channel, DAIMA_CHAN_WEBSOCKET) == 0) {
        return ws_server_send(chat_id, text);
    }
    if (strcmp(channel, DAIMA_CHAN_PET) == 0) {
        return ws_server_send_pet_response(chat_id, text);
    }
    if (strcmp(channel, DAIMA_CHAN_VOICE) == 0) {
        DAIMA_LOGI(TAG, "Voice output: [%s]", text);
        return tts_player_speak(text);
    }
    if (strcmp(channel, DAIMA_CHAN_FEISHU) == 0) {
        return feishu_send_card(chat_id, text);
    }
    if (strcmp(channel, DAIMA_CHAN_SYSTEM) == 0) {
        DAIMA_LOGI(TAG, "System message [%s]: %.128s", chat_id, text);
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
    return channel_runtime_send_text(msg->channel, msg->chat_id, msg->content);
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

daima_err_t channel_runtime_request_sudo(const daima_msg_t *msg,
                                        const char *request_id,
                                        const char *prompt_text)
{
    if (!msg || !request_id || !prompt_text) {
        return DAIMA_ERR_INVALID_ARG;
    }
    if (strcmp(msg->channel, DAIMA_CHAN_WEBSOCKET) == 0) {
        return ws_server_send_sudo_request(msg->chat_id, request_id, prompt_text);
    }
    return DAIMA_FAIL;
}

static bool parse_sudo_password_reply(const char *payload,
                                      const char *request_id,
                                      char *password_out,
                                      size_t password_out_size,
                                      bool *cancelled_out)
{
    if (!payload || !request_id || !password_out || password_out_size == 0) {
        return false;
    }
    const char *prefix = "__sudo_password__:";
    size_t prefix_len = strlen(prefix);
    if (strncmp(payload, prefix, prefix_len) != 0) {
        return false;
    }

    const char *rest = payload + prefix_len;
    const char *sep1 = strchr(rest, ':');
    const char *sep2 = sep1 ? strrchr(sep1 + 1, ':') : NULL;
    if (!sep1 || !sep2 || sep2 <= sep1) {
        return false;
    }

    size_t rid_len = (size_t)(sep1 - rest);
    if (strlen(request_id) != rid_len || strncmp(rest, request_id, rid_len) != 0) {
        return false;
    }

    size_t pwd_len = (size_t)(sep2 - (sep1 + 1));
    if (pwd_len >= password_out_size) pwd_len = password_out_size - 1;
    memcpy(password_out, sep1 + 1, pwd_len);
    password_out[pwd_len] = '\0';
    if (cancelled_out) {
        *cancelled_out = (*(sep2 + 1) == '1');
    }
    return true;
}

bool channel_runtime_wait_sudo_password(const daima_msg_t *msg,
                                        const char *request_id,
                                        char *password_out,
                                        size_t password_out_size)
{
    if (!msg || !request_id || !password_out || password_out_size == 0) {
        return false;
    }
    if (channel_runtime_request_sudo(
            msg,
            request_id,
            "This command requires sudo privileges. Please enter your sudo password to continue.") != DAIMA_OK) {
        return false;
    }

    time_t deadline = time(NULL) + 180;
    daima_msg_t deferred[16];
    int deferred_count = 0;
    bool got_password = false;
    bool cancelled = false;

    while (time(NULL) < deadline) {
        daima_msg_t incoming = {0};
        daima_err_t err = message_bus_pop_inbound(&incoming, 1000);
        if (err != DAIMA_OK) {
            continue;
        }

        if (strcmp(incoming.channel, msg->channel) == 0 &&
            strcmp(incoming.chat_id, msg->chat_id) == 0 &&
            parse_sudo_password_reply(incoming.content, request_id, password_out, password_out_size, &cancelled)) {
            free(incoming.content);
            free(incoming.image_path);
            if (!cancelled && password_out[0]) {
                got_password = true;
            }
            break;
        }

        if (deferred_count < (int)(sizeof(deferred) / sizeof(deferred[0]))) {
            deferred[deferred_count++] = incoming;
        } else {
            message_bus_push_inbound(&incoming);
            usleep(50 * 1000);
        }
    }

    for (int i = 0; i < deferred_count; i++) {
        message_bus_push_inbound(&deferred[i]);
    }
    return got_password;
}
