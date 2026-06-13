/* 飞书事件解析与消息入站处理。 */

#include "drivers/channel/feishu/feishu_event_handler.h"

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "bus.h"
#include "drivers/channel/feishu/feishu_api.h"
#include "drivers/channel/feishu/feishu_media.h"
#include "drivers/channel/feishu/feishu_message.h"
#include "drivers/channel/feishu/feishu_targets.h"
#include "cJSON.h"
#include "linux/printk.h"
#include "linux/slab.h"

static const char *TAG = "feishu_event";

#define FEISHU_DEDUP_CACHE_SIZE 64

static uint64_t s_seen_msg_keys[FEISHU_DEDUP_CACHE_SIZE] = {0};
static size_t s_seen_msg_idx = 0;

static uint64_t fnv1a64(const char *s)
{
    uint64_t h = 1469598103934665603ULL;
    if (!s) return h;
    while (*s) {
        h ^= (unsigned char)(*s++);
        h *= 1099511628211ULL;
    }
    return h;
}

static bool dedup_check_and_record(const char *message_id)
{
    uint64_t key = fnv1a64(message_id);
    for (size_t i = 0; i < FEISHU_DEDUP_CACHE_SIZE; i++) {
        if (s_seen_msg_keys[i] == key) return true;
    }
    s_seen_msg_keys[s_seen_msg_idx] = key;
    s_seen_msg_idx = (s_seen_msg_idx + 1) % FEISHU_DEDUP_CACHE_SIZE;
    return false;
}

static char *download_message_image(const char *app_id,
                                    const char *app_secret,
                                    const char *message_id,
                                    const char *image_key)
{
    char token[512];
    daima_err_t err = feishu_api_get_tenant_token(app_id, app_secret, token, sizeof(token));
    if (err != DAIMA_OK) {
        return NULL;
    }
    return feishu_download_message_image(token, message_id, image_key);
}

static void handle_message_event(const char *app_id, const char *app_secret, cJSON *event)
{
    cJSON *message = cJSON_GetObjectItem(event, "message");
    if (!message) return;

    cJSON *message_id_j = cJSON_GetObjectItem(message, "message_id");
    cJSON *chat_id_j = cJSON_GetObjectItem(message, "chat_id");
    cJSON *chat_type_j = cJSON_GetObjectItem(message, "chat_type");
    cJSON *msg_type_j = cJSON_GetObjectItem(message, "message_type");
    cJSON *content_j = cJSON_GetObjectItem(message, "content");

    if (!chat_id_j || !cJSON_IsString(chat_id_j)) return;
    if (!content_j || !cJSON_IsString(content_j)) return;

    const char *message_id = cJSON_IsString(message_id_j) ? message_id_j->valuestring : "";
    const char *chat_id = chat_id_j->valuestring;
    const char *chat_type = cJSON_IsString(chat_type_j) ? chat_type_j->valuestring : "p2p";
    const char *msg_type = cJSON_IsString(msg_type_j) ? msg_type_j->valuestring : "text";

    if (message_id[0] && dedup_check_and_record(message_id)) {
        DAIMA_LOGD(TAG, "Duplicate message %s, skipping", message_id);
        return;
    }

    cJSON *content_obj = cJSON_Parse(content_j->valuestring);
    if (!content_obj) {
        DAIMA_LOGW(TAG, "Failed to parse message content JSON");
        return;
    }

    const char *cleaned = NULL;
    char *image_path = NULL;
    char cleaned_buf[1024] = {0};
    char prompt_buf[1024] = {0};

    if (strcmp(msg_type, "text") == 0) {
        cJSON *text_j = cJSON_GetObjectItem(content_obj, "text");
        if (!text_j || !cJSON_IsString(text_j)) {
            cJSON_Delete(content_obj);
            return;
        }

        feishu_normalize_text(text_j->valuestring, cleaned_buf, sizeof(cleaned_buf));
        if (!cleaned_buf[0]) {
            cJSON_Delete(content_obj);
            return;
        }
        cleaned = cleaned_buf;
    } else if (strcmp(msg_type, "image") == 0) {
        cJSON *image_key_j = cJSON_GetObjectItem(content_obj, "image_key");
        cJSON *text_j = cJSON_GetObjectItem(content_obj, "text");
        cJSON *alt_j = cJSON_GetObjectItem(content_obj, "alt");
        const char *image_key = cJSON_IsString(image_key_j) ? image_key_j->valuestring : "";
        const char *extra_text = cJSON_IsString(text_j) ? text_j->valuestring : NULL;
        if ((!extra_text || !extra_text[0]) && cJSON_IsString(alt_j)) {
            extra_text = alt_j->valuestring;
        }
        image_path = download_message_image(app_id, app_secret, message_id, image_key);
        if (!image_path) {
            cJSON_Delete(content_obj);
            return;
        }
        feishu_build_image_prompt(extra_text, prompt_buf, sizeof(prompt_buf));
        cleaned = prompt_buf;
    } else if (strcmp(msg_type, "post") == 0) {
        const char *image_key = NULL;
        feishu_collect_post_parts(content_obj, cleaned_buf, sizeof(cleaned_buf), &image_key);
        if (image_key && image_key[0]) {
            image_path = download_message_image(app_id, app_secret, message_id, image_key);
            if (!image_path) {
                cJSON_Delete(content_obj);
                return;
            }
            feishu_build_image_prompt(cleaned_buf, prompt_buf, sizeof(prompt_buf));
            cleaned = prompt_buf;
        } else if (cleaned_buf[0]) {
            cleaned = cleaned_buf;
        } else {
            DAIMA_LOGI(TAG, "Ignoring empty Feishu post message");
            cJSON_Delete(content_obj);
            return;
        }
    } else {
        DAIMA_LOGI(TAG, "Ignoring unsupported Feishu message type: %s", msg_type);
        cJSON_Delete(content_obj);
        return;
    }

    const char *sender_id = "";
    cJSON *sender = cJSON_GetObjectItem(event, "sender");
    if (sender) {
        cJSON *sender_id_obj = cJSON_GetObjectItem(sender, "sender_id");
        if (sender_id_obj) {
            cJSON *open_id = cJSON_GetObjectItem(sender_id_obj, "open_id");
            if (open_id && cJSON_IsString(open_id)) {
                sender_id = open_id->valuestring;
            }
        }
    }

    DAIMA_LOGI(TAG, "Message from %s in %s(%s) type=%s: %.60s%s",
              sender_id, chat_id, chat_type, msg_type, cleaned,
              strlen(cleaned) > 60 ? "..." : "");

    const char *route_id = chat_id;
    if (strcmp(chat_type, "p2p") == 0 && sender_id[0]) {
        route_id = sender_id;
    }
    feishu_targets_record(route_id, chat_id, chat_type, sender_id);

    daima_msg_t msg = {0};
    strncpy(msg.channel, DAIMA_CHAN_FEISHU, sizeof(msg.channel) - 1);
    strncpy(msg.chat_id, route_id, sizeof(msg.chat_id) - 1);
    strncpy(msg.source, DAIMA_MSG_SOURCE_USER, sizeof(msg.source) - 1);
    msg.content = strdup(cleaned);
    msg.image_path = image_path;

    if (msg.content) {
        if (message_bus_push_inbound(&msg) != DAIMA_OK) {
            DAIMA_LOGW(TAG, "Inbound queue full, dropping feishu message");
            kfree(msg.content);
            if (msg.image_path) unlink(msg.image_path);
            kfree(msg.image_path);
        }
    } else {
        if (msg.image_path) unlink(msg.image_path);
        kfree(msg.image_path);
    }

    cJSON_Delete(content_obj);
}

void feishu_event_handler_process_ws_event_json(const char *app_id,
                                                const char *app_secret,
                                                const char *json,
                                                size_t len)
{
    if (!app_id || !app_id[0] || !app_secret || !app_secret[0] || !json || len == 0) {
        return;
    }

    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) return;

    cJSON *event = cJSON_GetObjectItem(root, "event");
    cJSON *header = cJSON_GetObjectItem(root, "header");
    if (event && header) {
        cJSON *event_type = cJSON_GetObjectItem(header, "event_type");
        if (event_type && cJSON_IsString(event_type) &&
            strcmp(event_type->valuestring, "im.message.receive_v1") == 0) {
            handle_message_event(app_id, app_secret, event);
        }
    } else if (event) {
        handle_message_event(app_id, app_secret, event);
    }

    cJSON_Delete(root);
}
