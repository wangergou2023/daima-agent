#include "interactive.h"

#include <string.h>
#include <time.h>
#include <unistd.h>

#include "drivers/channel/gateway/ws_server.h"
#include "linux/slab.h"

err_t channel_runtime_request_sudo(const struct message *msg,
                                        const char *request_id,
                                        const char *prompt_text)
{
    if (!msg || !request_id || !prompt_text) {
        return ERR_INVALID_ARG;
    }
    if (strcmp(msg->channel, DAIMA_CHAN_WEBSOCKET) == 0) {
        return ws_server_send_sudo_request(msg->chat_id, request_id, prompt_text);
    }
    return ERR_FAIL;
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

bool channel_runtime_wait_sudo_password(const struct message *msg,
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
            "This command requires sudo privileges. Please enter your sudo password to continue.") != 0) {
        return false;
    }

    time_t deadline = time(NULL) + 180;
    struct message deferred[16];
    int deferred_count = 0;
    bool got_password = false;
    bool cancelled = false;

    while (time(NULL) < deadline) {
        struct message incoming = {0};
        err_t err = message_bus_pop_inbound(&incoming, 1000);
        if (err != 0) {
            continue;
        }

        if (strcmp(incoming.channel, msg->channel) == 0 &&
            strcmp(incoming.chat_id, msg->chat_id) == 0 &&
            parse_sudo_password_reply(incoming.content, request_id, password_out, password_out_size, &cancelled)) {
            kfree(incoming.content);
            kfree(incoming.image_path);
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
