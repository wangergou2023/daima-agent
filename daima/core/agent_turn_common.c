#include "core/agent_turn_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "core/config.h"
#include "core/env.h"

int agent_env_int_or_default(const char *name, int fallback)
{
    const char *raw = getenv(name);
    if (!raw || !raw[0]) {
        return fallback;
    }
    char *end = NULL;
    long val = strtol(raw, &end, 10);
    if (end == raw || (end && *end != '\0')) {
        return fallback;
    }
    return (int)val;
}

bool agent_env_bool_or_default(const char *name, bool fallback)
{
    const char *raw = getenv(name);
    if (!raw || !raw[0]) {
        return fallback;
    }
    if (strcmp(raw, "1") == 0 || strcasecmp(raw, "true") == 0 || strcasecmp(raw, "yes") == 0) {
        return true;
    }
    if (strcmp(raw, "0") == 0 || strcasecmp(raw, "false") == 0 || strcasecmp(raw, "no") == 0) {
        return false;
    }
    return fallback;
}


const char *agent_msg_source_or_default(const daima_msg_t *msg)
{
    if (!msg) {
        return DAIMA_MSG_SOURCE_USER;
    }
    if (msg->source[0]) {
        return msg->source;
    }
    return DAIMA_MSG_SOURCE_USER;
}

bool agent_msg_is_internal_control(const daima_msg_t *msg)
{
    return strcmp(agent_msg_source_or_default(msg), DAIMA_MSG_SOURCE_INTERNAL) == 0;
}

bool agent_msg_is_synthetic_event(const daima_msg_t *msg)
{
    const char *source = agent_msg_source_or_default(msg);
    if (strcmp(source, DAIMA_MSG_SOURCE_CRON) == 0 ||
        strcmp(source, DAIMA_MSG_SOURCE_HEARTBEAT) == 0 ||
        strcmp(source, DAIMA_MSG_SOURCE_INTERNAL) == 0) {
        return true;
    }
    return msg && strcmp(msg->channel, DAIMA_CHAN_SYSTEM) == 0;
}

const char *agent_msg_role_for_current_turn(const daima_msg_t *msg)
{
    return agent_msg_is_synthetic_event(msg) ? "system" : "user";
}

const char *agent_session_role_for_inbound_msg(const daima_msg_t *msg)
{
    if (!msg || !msg->content || !msg->content[0]) {
        return NULL;
    }

    const char *source = agent_msg_source_or_default(msg);
    if (strcmp(source, DAIMA_MSG_SOURCE_USER) == 0) {
        return "user";
    }
    if (strcmp(source, DAIMA_MSG_SOURCE_INTERNAL) == 0) {
        return NULL;
    }
    return "system";
}

void agent_chat_id_to_slug(const char *chat_id, char *buf, size_t size)
{
    if (!buf || size == 0) {
        return;
    }
    buf[0] = '\0';
    if (!chat_id || !chat_id[0]) {
        snprintf(buf, size, "unknown");
        return;
    }

    size_t off = 0;
    for (const char *p = chat_id; *p && off + 1 < size; ++p) {
        char ch = *p;
        if ((ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '_' || ch == '-') {
            buf[off++] = ch;
        } else {
            buf[off++] = '_';
        }
    }
    buf[off] = '\0';
    if (off == 0) {
        snprintf(buf, size, "unknown");
    }
}

void agent_cleanup_inbound_msg(daima_msg_t *msg)
{
    if (!msg) {
        return;
    }

    free(msg->content);
    msg->content = NULL;

    if (msg->image_path) {
        unlink(msg->image_path);
    }
    free(msg->image_path);
    msg->image_path = NULL;
}

void agent_cleanup_outbound_msg(daima_msg_t *msg)
{
    if (!msg) {
        return;
    }

    free(msg->content);
    msg->content = NULL;
    free(msg->reasoning);
    msg->reasoning = NULL;
}
