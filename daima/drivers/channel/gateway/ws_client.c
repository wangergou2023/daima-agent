/* WebSocket 客户端会话：连接表、消息处理、keepalive。 */

#include "drivers/channel/gateway/ws_client.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "cancel.h"
#include "fs.h"
#include "paths.h"
#include "bus.h"
#include "drivers/channel/gateway/ws_server.h"
#include "drivers/pet/pet_event.h"
#include "autoconf.h"
#include "linux/list.h"
#include "linux/printk.h"
#include "cJSON.h"
#include "linux/slab.h"
#include "linux/kernel.h"
#define WS_PING_INTERVAL WS_PING_INTERVAL_SEC
#define WS_PONG_TIMEOUT  WS_PONG_TIMEOUT_SEC
#define WS_MAX_TEXT_BYTES (64 * 1024)
#ifdef ENABLE_VISION
#define WS_MAX_UPLOAD_BYTES VISION_MAX_IMAGE_SIZE
#else
#define WS_MAX_UPLOAD_BYTES (10 * 1024 * 1024)
#endif

typedef struct {
    struct list_head list;
    int fd;
    char chat_id[32];
    bool upload_pending;
    char upload_chat_id[64];
    char upload_filename[128];
    char upload_mime_type[64];
    size_t upload_size;
    bool active;
    time_t last_seen;
    time_t last_ping;
    time_t last_pong;
    bool awaiting_pong;
} ws_client_t;

static ws_client_t s_clients[WS_MAX_CLIENTS];
static LIST_HEAD(s_client_list);
static pthread_mutex_t s_clients_mutex = PTHREAD_MUTEX_INITIALIZER;
static char s_pending_response[65536];
static bool s_has_pending = false;
static pthread_mutex_t s_pending_mutex = PTHREAD_MUTEX_INITIALIZER;

static int recv_all(int fd, void *buf, size_t len)
{
    size_t off = 0;
    while (off < len) {
        ssize_t r = recv(fd, (char *)buf + off, len - off, 0);
        if (r <= 0) return -1;
        off += (size_t)r;
    }
    return 0;
}

static int send_all(int fd, const void *buf, size_t len)
{
    const char *p = (const char *)buf;
    size_t off = 0;
    while (off < len) {
        ssize_t n = send(fd, p + off, len - off, 0);
        if (n <= 0) return -1;
        off += (size_t)n;
    }
    return 0;
}

static int ws_send_control(int fd, unsigned char opcode, const void *payload, size_t len)
{
    if (len > 125) len = 125;
    unsigned char header[2];
    header[0] = 0x80 | (opcode & 0x0F);
    header[1] = (unsigned char)len;
    if (send_all(fd, header, sizeof(header)) != 0) return -1;
    if (len > 0 && payload) {
        if (send_all(fd, payload, len) != 0) return -1;
    }
    return 0;
}

static int ws_send_ping(int fd)
{
    return ws_send_control(fd, 0x9, NULL, 0);
}

static int ws_send_pong(int fd, const void *payload, size_t len)
{
    return ws_send_control(fd, 0xA, payload, len);
}

static int ws_send_text(int fd, const char *text)
{
    size_t len = strlen(text);
    unsigned char header[10];
    size_t hlen = 0;
    header[0] = 0x81;

    if (len < 126) {
        header[1] = (unsigned char)len;
        hlen = 2;
    } else if (len <= 0xFFFF) {
        header[1] = 126;
        header[2] = (len >> 8) & 0xFF;
        header[3] = len & 0xFF;
        hlen = 4;
    } else {
        header[1] = 127;
        for (int i = 0; i < 8; i++) {
            header[2 + i] = (len >> (56 - 8 * i)) & 0xFF;
        }
        hlen = 10;
    }

    if (send(fd, header, hlen, 0) != (ssize_t)hlen) return -1;
    if (send(fd, text, len, 0) != (ssize_t)len) return -1;
    return 0;
}

static void ws_send_json_text(int fd, cJSON *obj)
{
    char *text = cJSON_PrintUnformatted(obj);
    if (!text) return;
    ws_send_text(fd, text);
    kfree(text);
}

void ws_pending_save(const char *response_text)
{
    if (!response_text || !response_text[0]) {
        return;
    }

    pthread_mutex_lock(&s_pending_mutex);
    strscpy(s_pending_response, response_text, sizeof(s_pending_response));
    s_has_pending = true;
    pthread_mutex_unlock(&s_pending_mutex);
}

const char *ws_pending_pop(void)
{
    const char *response = NULL;

    pthread_mutex_lock(&s_pending_mutex);
    if (s_has_pending) {
        s_has_pending = false;
        response = s_pending_response[0] ? s_pending_response : NULL;
    }
    pthread_mutex_unlock(&s_pending_mutex);
    return response;
}

static void ws_pending_restore(void)
{
    pthread_mutex_lock(&s_pending_mutex);
    if (s_pending_response[0]) {
        s_has_pending = true;
    }
    pthread_mutex_unlock(&s_pending_mutex);
}

static int ws_send_pending_response(int fd, const char *chat_id, const char *response_text)
{
    if (!response_text || !response_text[0]) {
        return 0;
    }

    cJSON *resp = cJSON_CreateObject();
    if (!resp) {
        return -1;
    }
    cJSON_AddStringToObject(resp, "type", "response");
    cJSON_AddStringToObject(resp, "content", response_text);
    cJSON_AddStringToObject(resp, "chat_id", chat_id ? chat_id : "");
    char *text = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);
    if (!text) {
        return -1;
    }
    int ret = ws_send_text(fd, text);
    kfree(text);
    return ret;
}

static void ws_send_upload_error(int fd, const char *chat_id, const char *error)
{
    cJSON *resp = cJSON_CreateObject();
    if (!resp) return;
    cJSON_AddStringToObject(resp, "type", "upload_error");
    if (chat_id && chat_id[0]) {
        cJSON_AddStringToObject(resp, "chat_id", chat_id);
    }
    cJSON_AddStringToObject(resp, "error", error ? error : "upload failed");
    ws_send_json_text(fd, resp);
    cJSON_Delete(resp);
}

static const char *file_ext(const char *filename)
{
    const char *dot = filename ? strrchr(filename, '.') : NULL;
    return dot && dot[1] ? dot + 1 : "";
}

static bool is_supported_image_ext(const char *ext)
{
    return ext
        && (strcasecmp(ext, "png") == 0
            || strcasecmp(ext, "jpg") == 0
            || strcasecmp(ext, "jpeg") == 0
            || strcasecmp(ext, "webp") == 0
            || strcasecmp(ext, "gif") == 0);
}

static bool is_supported_image_mime(const char *mime)
{
    return mime
        && (strcasecmp(mime, "image/png") == 0
            || strcasecmp(mime, "image/jpeg") == 0
            || strcasecmp(mime, "image/webp") == 0
            || strcasecmp(mime, "image/gif") == 0);
}

static bool sanitize_filename(const char *src, char *dst, size_t dst_size)
{
    if (!src || !dst || dst_size == 0) return false;

    size_t off = 0;
    for (const char *p = src; *p && off + 1 < dst_size; p++) {
        char ch = *p;
        if ((ch >= 'a' && ch <= 'z')
            || (ch >= 'A' && ch <= 'Z')
            || (ch >= '0' && ch <= '9')
            || ch == '.' || ch == '_' || ch == '-') {
            dst[off++] = ch;
        } else if (ch == ' ') {
            dst[off++] = '_';
        }
    }
    dst[off] = '\0';
    return off > 0;
}

static bool save_uploaded_image(ws_client_t *client, const char *payload, uint64_t len,
                                char *saved_path, size_t saved_path_size)
{
    if (!client || !payload || !saved_path || saved_path_size == 0) return false;
    if (!client->upload_pending || len == 0 || len > WS_MAX_UPLOAD_BYTES) return false;
    if (client->upload_size > 0 && len != client->upload_size) return false;

    const char *ext = file_ext(client->upload_filename);
    if (!is_supported_image_ext(ext)) return false;

    char safe_name[128];
    if (!sanitize_filename(client->upload_filename, safe_name, sizeof(safe_name))) {
        snprintf(safe_name, sizeof(safe_name), "upload.%s", ext);
    }

    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);

    char day[16];
    strftime(day, sizeof(day), "%Y-%m-%d", &tm_now);

    char upload_root[512];
    snprintf(upload_root, sizeof(upload_root), "%s/uploads/%s", daima_path_spiffs_base(), day);
    if (!daima_fs_ensure_dir_recursive(upload_root)) {
        return false;
    }

    snprintf(saved_path, saved_path_size, "%s/%ld_%d_%s", upload_root, (long)now, client->fd, safe_name);
    FILE *fp = fopen(saved_path, "wb");
    if (!fp) return false;
    size_t written = fwrite(payload, 1, (size_t)len, fp);
    int close_ret = fclose(fp);
    if (written != (size_t)len || close_ret != 0) {
        unlink(saved_path);
        return false;
    }
    return true;
}

static bool is_allowed_uploaded_image_path(const char *path)
{
    if (!path || !path[0]) return false;
    const char *base = daima_path_spiffs_base();
    if (!base || !base[0]) return false;

    char prefix[512];
    snprintf(prefix, sizeof(prefix), "%s/uploads/", base);
    size_t prefix_len = strlen(prefix);
    if (strncmp(path, prefix, prefix_len) != 0) return false;
    if (strstr(path + prefix_len, "/../") || strstr(path + prefix_len, "../")) return false;
    return is_supported_image_ext(file_ext(path));
}

static ws_client_t *find_client_by_fd(int fd)
{
    ws_client_t *client;

    list_for_each_entry(client, &s_client_list, list, ws_client_t) {
        if (client->fd == fd) {
            return client;
        }
    }
    return NULL;
}

static ws_client_t *find_client_by_chat_id(const char *chat_id)
{
    ws_client_t *client;

    list_for_each_entry(client, &s_client_list, list, ws_client_t) {
        if (strcmp(client->chat_id, chat_id) == 0) {
            return client;
        }
    }
    return NULL;
}

static void ws_client_detach(ws_client_t *client)
{
    if (!client || !client->active) return;

    client->active = false;
    list_del(&client->list);
}

static void clear_upload_state(ws_client_t *client)
{
    if (!client) return;
    client->upload_pending = false;
    client->upload_chat_id[0] = '\0';
    client->upload_filename[0] = '\0';
    client->upload_mime_type[0] = '\0';
    client->upload_size = 0;
}

static void remove_client(int fd)
{
    pthread_mutex_lock(&s_clients_mutex);
    ws_client_t *client = find_client_by_fd(fd);
    if (client) {
        pr_info("Client disconnected: %s", client->chat_id);
        ws_client_detach(client);
        close(client->fd);
        client->fd = -1;
        clear_upload_state(client);
    }
    pthread_mutex_unlock(&s_clients_mutex);
}

static void client_touch(int fd, time_t now, bool pong)
{
    pthread_mutex_lock(&s_clients_mutex);
    ws_client_t *client = find_client_by_fd(fd);
    if (client) {
        client->last_seen = now;
        if (pong) {
            client->last_pong = now;
            client->awaiting_pong = false;
        }
    }
    pthread_mutex_unlock(&s_clients_mutex);
}

static void drop_duplicate_chat_id(const char *chat_id, int keep_fd)
{
    if (!chat_id || !chat_id[0]) return;
    pthread_mutex_lock(&s_clients_mutex);
    ws_client_t *client, *next;
    list_for_each_entry_safe(client, next, &s_client_list, list, ws_client_t) {
        if (client->fd != keep_fd && strcmp(client->chat_id, chat_id) == 0) {
            int old_fd = client->fd;
            ws_client_detach(client);
            client->fd = -1;
            client->chat_id[0] = '\0';
            clear_upload_state(client);
            pthread_mutex_unlock(&s_clients_mutex);
            if (old_fd >= 0) close(old_fd);
            pr_warn("Dropped duplicate chat_id=%s (fd=%d)", chat_id, old_fd);
            pthread_mutex_lock(&s_clients_mutex);
        }
    }
    pthread_mutex_unlock(&s_clients_mutex);
}

static const char *resolve_client_chat_id(ws_client_t *client, cJSON *root, int fd)
{
    const char *chat_id = client ? client->chat_id : "ws_unknown";
    cJSON *cid = cJSON_GetObjectItem(root, "chat_id");
    if (cid && cJSON_IsString(cid) && cid->valuestring[0]) {
        chat_id = cid->valuestring;
        drop_duplicate_chat_id(chat_id, fd);
        if (client) {
            strscpy(client->chat_id, chat_id, sizeof(client->chat_id));
        }
    }
    return chat_id;
}

daima_err_t ws_client_session_send_json(const char *chat_id, cJSON *obj)
{
    if (!chat_id || !obj) {
        return DAIMA_ERR_INVALID_ARG;
    }

    ws_client_t *client = NULL;
    pthread_mutex_lock(&s_clients_mutex);
    client = find_client_by_chat_id(chat_id);
    if (!client) {
        pthread_mutex_unlock(&s_clients_mutex);
        pr_warn("No WS client with chat_id=%s", chat_id);
        return DAIMA_ERR_NOT_FOUND;
    }
    int fd = client->fd;
    pthread_mutex_unlock(&s_clients_mutex);

    char *json_str = cJSON_PrintUnformatted(obj);
    if (!json_str) {
        return DAIMA_ERR_NO_MEM;
    }
    int ret = ws_send_text(fd, json_str);
    kfree(json_str);
    if (ret != 0) {
        pr_warn("Failed to send JSON to %s", chat_id);
        remove_client(fd);
        return DAIMA_FAIL;
    }
    return DAIMA_OK;
}

bool ws_client_session_add(int fd)
{
    time_t now = time(NULL);
    int slot = -1;
    ws_client_t *evict = NULL;
    time_t max_idle = 0;

    pthread_mutex_lock(&s_clients_mutex);
    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        if (!s_clients[i].active) {
            slot = i;
            break;
        }
    }

    ws_client_t *client;
    list_for_each_entry(client, &s_client_list, list, ws_client_t) {
        time_t idle = now - client->last_seen;
        if (idle > WS_PONG_TIMEOUT && idle > max_idle) {
            max_idle = idle;
            evict = client;
        }
    }

    if (slot < 0 && evict) {
        int old_fd = evict->fd;
        char old_chat[32];
        strscpy(old_chat, evict->chat_id, sizeof(old_chat));
        slot = (int)(evict - s_clients);
        ws_client_detach(evict);
        evict->fd = -1;
        evict->chat_id[0] = '\0';
        clear_upload_state(evict);
        pthread_mutex_unlock(&s_clients_mutex);
        if (old_fd >= 0) close(old_fd);
        pr_warn("Evicted stale client %s (fd=%d)", old_chat, old_fd);
        pthread_mutex_lock(&s_clients_mutex);
    }

    if (slot >= 0) {
        s_clients[slot].fd = fd;
        snprintf(s_clients[slot].chat_id, sizeof(s_clients[slot].chat_id), "ws_%d", fd);
        char chat_id[sizeof(s_clients[slot].chat_id)];
        strscpy(chat_id, s_clients[slot].chat_id, sizeof(chat_id));
        clear_upload_state(&s_clients[slot]);
        s_clients[slot].last_seen = now;
        s_clients[slot].last_pong = now;
        s_clients[slot].last_ping = now;
        s_clients[slot].awaiting_pong = false;
        s_clients[slot].active = true;
        list_add(&s_clients[slot].list, &s_client_list);
        pthread_mutex_unlock(&s_clients_mutex);
        pr_info("Client connected: %s (fd=%d)", chat_id, fd);

        const char *pending = ws_pending_pop();
        if (pending) {
            if (ws_send_pending_response(fd, chat_id, pending) != 0) {
                ws_pending_restore();
                remove_client(fd);
                return true;
            }
            pr_info("Delivered pending response to %s", chat_id);
        }
        return true;
    }

    pthread_mutex_unlock(&s_clients_mutex);
    pr_warn("Max clients reached, rejecting fd=%d", fd);
    return false;
}
static bool ws_read_frame_header(int fd, unsigned char *out_opcode, uint64_t *out_len, bool *out_masked, unsigned char *out_mask)
{
    unsigned char hdr[2];
    if (recv_all(fd, hdr, 2) != 0) {
        return false;
    }

    *out_opcode = hdr[0] & 0x0F;
    *out_masked = (hdr[1] & 0x80) != 0;
    *out_len = hdr[1] & 0x7F;

    if (*out_len == 126) {
        unsigned char ext[2];
        if (recv_all(fd, ext, 2) != 0) { return false; }
        *out_len = ((uint64_t)ext[0] << 8) | ext[1];
    } else if (*out_len == 127) {
        unsigned char ext[8];
        if (recv_all(fd, ext, 8) != 0) { return false; }
        *out_len = 0;
        for (int i = 0; i < 8; i++) {
            *out_len = (*out_len << 8) | ext[i];
        }
    }

    if (*out_masked) {
        if (recv_all(fd, out_mask, 4) != 0) { return false; }
    }

    return true;
}

static char *ws_read_frame_payload(int fd, uint64_t len, bool masked, const unsigned char *mask)
{
    char *payload = kzalloc(len + 1, GFP_KERNEL);
    if (!payload) { return NULL; }

    if (recv_all(fd, payload, len) != 0) {
        kfree(payload);
        return NULL;
    }

    if (masked) {
        for (uint64_t i = 0; i < len; i++) {
            payload[i] ^= mask[i % 4];
        }
    }
    payload[len] = '\0';
    return payload;
}

static void ws_handle_close_frame(int fd)
{
    remove_client(fd);
}

static void ws_handle_ping_frame(int fd, const char *payload, size_t len, time_t now)
{
    ws_send_pong(fd, payload, len);
    client_touch(fd, now, true);
}

static void ws_handle_pong_frame(int fd, time_t now)
{
    client_touch(fd, now, true);
}

static void ws_handle_binary_frame(int fd, ws_client_t *client, const char *payload, uint64_t len, time_t now)
{
    client_touch(fd, now, false);
    if (!client || !client->upload_pending) {
        ws_send_upload_error(fd, client ? client->chat_id : NULL, "no pending upload");
        return;
    }

    char saved_path[768];
    bool ok = save_uploaded_image(client, payload, len, saved_path, sizeof(saved_path));
    if (!ok) {
        ws_send_upload_error(fd, client->upload_chat_id, "failed to save image");
        pr_warn("Image upload save failed chat=%s name=%s len=%llu", client->upload_chat_id, client->upload_filename, (unsigned long long)len);
        clear_upload_state(client);
        return;
    }

    cJSON *resp = cJSON_CreateObject();
    if (resp) {
        cJSON_AddStringToObject(resp, "type", "upload_done");
        cJSON_AddStringToObject(resp, "chat_id", client->upload_chat_id);
        cJSON_AddStringToObject(resp, "image_path", saved_path);
        cJSON_AddStringToObject(resp, "filename", client->upload_filename);
        ws_send_json_text(fd, resp);
        cJSON_Delete(resp);
    }
    pr_info("Image upload saved chat=%s size=%llu path=%s", client->upload_chat_id, (unsigned long long)len, saved_path);
    clear_upload_state(client);
}

static void ws_handle_upload_request(int fd, ws_client_t *client, cJSON *root)
{
    const char *chat_id = resolve_client_chat_id(client, root, fd);
    cJSON *filename = cJSON_GetObjectItem(root, "filename");
    cJSON *mime_type = cJSON_GetObjectItem(root, "mime_type");
    cJSON *size_json = cJSON_GetObjectItem(root, "size");
    double size_value = size_json && cJSON_IsNumber(size_json) ? size_json->valuedouble : 0;
    const char *name = filename && cJSON_IsString(filename) ? filename->valuestring : "";
    const char *mime = mime_type && cJSON_IsString(mime_type) ? mime_type->valuestring : "";
    const char *ext = file_ext(name);

    if (!client || size_value <= 0 || size_value > (double)WS_MAX_UPLOAD_BYTES
        || !is_supported_image_ext(ext) || !is_supported_image_mime(mime)) {
        ws_send_upload_error(fd, chat_id, "unsupported image upload");
        return;
    }

    client->upload_pending = true;
    strscpy(client->upload_chat_id, chat_id, sizeof(client->upload_chat_id));
    strscpy(client->upload_filename, name, sizeof(client->upload_filename));
    strscpy(client->upload_mime_type, mime, sizeof(client->upload_mime_type));
    client->upload_size = (size_t)size_value;
    pr_info("Image upload pending chat=%s name=%s mime=%s size=%zu", client->upload_chat_id, client->upload_filename, client->upload_mime_type, client->upload_size);
}

static void ws_handle_chat_message(int fd, ws_client_t *client, cJSON *root)
{
    cJSON *content = cJSON_GetObjectItem(root, "content");
    if (!content || !cJSON_IsString(content)) {
        return;
    }

    const char *chat_id = resolve_client_chat_id(client, root, fd);
    cJSON *image_path = cJSON_GetObjectItem(root, "image_path");
    const char *image_path_value = image_path && cJSON_IsString(image_path) ? image_path->valuestring : NULL;

    bool valid_image_path = is_allowed_uploaded_image_path(image_path_value);
    pr_info("WS message from %s: %.40s... image=%s", chat_id, content->valuestring, valid_image_path ? "yes" : "no");
    agent_cancel_request(chat_id, "new_web_message");

    struct message msg = {0};
    strncpy(msg.channel, DAIMA_CHAN_WEBSOCKET, sizeof(msg.channel) - 1);
    strncpy(msg.chat_id, chat_id, sizeof(msg.chat_id) - 1);
    strncpy(msg.source, DAIMA_MSG_SOURCE_USER, sizeof(msg.source) - 1);
    msg.content = strdup(content->valuestring);
    if (valid_image_path) {
        msg.image_path = strdup(image_path_value);
    }
    if (msg.content) {
        message_bus_push_inbound(&msg);
    } else {
        kfree(msg.image_path);
    }
}

static void ws_handle_stop(int fd, ws_client_t *client, cJSON *root)
{
    const char *chat_id = resolve_client_chat_id(client, root, fd);

    pr_info("WS stop from %s", chat_id);
    agent_cancel_request(chat_id, "web_stop");

    cJSON *resp = cJSON_CreateObject();
    if (resp) {
        cJSON_AddStringToObject(resp, "type", "stopped");
        cJSON_AddStringToObject(resp, "chat_id", chat_id);
        char *text = cJSON_PrintUnformatted(resp);
        if (text) {
            ws_send_text(fd, text);
            kfree(text);
        }
        cJSON_Delete(resp);
    }
}

static void ws_handle_pet_action(int fd, ws_client_t *client, cJSON *root)
{
    const char *chat_id = resolve_client_chat_id(client, root, fd);
    const char *action = NULL;
    const char *pet_id = NULL;
    char pet_chat_id[64] = {0};

    cJSON *pet_cid = cJSON_GetObjectItem(root, "pet_chat_id");
    cJSON *action_json = cJSON_GetObjectItem(root, "action");
    cJSON *pet_id_json = cJSON_GetObjectItem(root, "pet_id");

    if (pet_cid && cJSON_IsString(pet_cid) && pet_cid->valuestring[0]) {
        strscpy(pet_chat_id, pet_cid->valuestring, sizeof(pet_chat_id));
    } else {
        if (!pet_build_chat_id(chat_id, pet_chat_id, sizeof(pet_chat_id))) {
            return;
        }
    }

    if (action_json && cJSON_IsString(action_json)) {
        action = action_json->valuestring;
    }
    if (pet_id_json && cJSON_IsString(pet_id_json)) {
        pet_id = pet_id_json->valuestring;
    }

    char *pet_prompt = pet_build_action_prompt(action, pet_id);
    if (pet_prompt) {
        struct message msg = {0};
        strncpy(msg.channel, DAIMA_CHAN_PET, sizeof(msg.channel) - 1);
        strncpy(msg.chat_id, pet_chat_id, sizeof(msg.chat_id) - 1);
        strncpy(msg.source, DAIMA_MSG_SOURCE_USER, sizeof(msg.source) - 1);
        msg.content = pet_prompt;
        message_bus_push_inbound(&msg);
    }
}

static void ws_handle_sudo_password(int fd, ws_client_t *client, cJSON *root)
{
    const char *chat_id = client ? client->chat_id : "ws_unknown";
    cJSON *cid = cJSON_GetObjectItem(root, "chat_id");
    cJSON *req = cJSON_GetObjectItem(root, "request_id");
    cJSON *pwd = cJSON_GetObjectItem(root, "password");
    cJSON *cancelled = cJSON_GetObjectItem(root, "cancelled");
    if (cid && cJSON_IsString(cid) && cid->valuestring[0]) {
        chat_id = cid->valuestring;
    }
    if (req && cJSON_IsString(req) && pwd && cJSON_IsString(pwd)) {
        size_t need = strlen("__sudo_password__::") + strlen(req->valuestring) + strlen(pwd->valuestring) + 8;
        char *payload2 = kzalloc(need, GFP_KERNEL);
        if (payload2) {
            snprintf(payload2, need, "__sudo_password__:%s:%s:%d",
                     req->valuestring,
                     pwd->valuestring,
                     (cancelled && cJSON_IsTrue(cancelled)) ? 1 : 0);
            struct message msg = {0};
            strncpy(msg.channel, DAIMA_CHAN_WEBSOCKET, sizeof(msg.channel) - 1);
            strncpy(msg.chat_id, chat_id, sizeof(msg.chat_id) - 1);
            strncpy(msg.source, DAIMA_MSG_SOURCE_INTERNAL, sizeof(msg.source) - 1);
            msg.content = payload2;
            message_bus_push_inbound(&msg);
        }
    }
}

static void ws_dispatch_text_frame(int fd, ws_client_t *client, const char *payload, time_t now)
{
    client_touch(fd, now, false);

    cJSON *root = cJSON_Parse(payload);
    if (!root) {
        pr_warn("Invalid JSON from fd=%d", fd);
        return;
    }

    cJSON *type = cJSON_GetObjectItem(root, "type");
    if (!type || !cJSON_IsString(type)) {
        cJSON_Delete(root);
        return;
    }
    const char *type_str = type->valuestring;

    if (strcmp(type_str, "ping") == 0) {
        ws_send_text(fd, "{\"type\":\"pong\"}");
    } else if (strcmp(type_str, "upload_image") == 0) {
        ws_handle_upload_request(fd, client, root);
    } else if (strcmp(type_str, "message") == 0) {
        ws_handle_chat_message(fd, client, root);
    } else if (strcmp(type_str, "stop") == 0) {
        ws_handle_stop(fd, client, root);
    } else if (strcmp(type_str, PET_WS_TYPE_ACTION) == 0) {
        ws_handle_pet_action(fd, client, root);
    } else if (strcmp(type_str, "sudo_password") == 0) {
        ws_handle_sudo_password(fd, client, root);
    }

    cJSON_Delete(root);
}

static void handle_message(int fd)
{
    unsigned char opcode;
    uint64_t len;
    bool masked;
    unsigned char mask[4] = {0};

    if (!ws_read_frame_header(fd, &opcode, &len, &masked, mask)) {
        remove_client(fd);
        return;
    }

    if ((opcode == 0x1 && len > WS_MAX_TEXT_BYTES)
        || (opcode == 0x2 && len > WS_MAX_UPLOAD_BYTES)
        || (opcode != 0x1 && opcode != 0x2 && len > 125)) {
        remove_client(fd);
        return;
    }

    char *payload = ws_read_frame_payload(fd, len, masked, mask);
    if (!payload) {
        remove_client(fd);
        return;
    }

    time_t now = time(NULL);
    ws_client_t *client = find_client_by_fd(fd);

    switch (opcode) {
        case 0x8: ws_handle_close_frame(fd); break;
        case 0x9: ws_handle_ping_frame(fd, payload, len, now); break;
        case 0xA: ws_handle_pong_frame(fd, now); break;
        case 0x2: ws_handle_binary_frame(fd, client, payload, len, now); break;
        case 0x1: ws_dispatch_text_frame(fd, client, payload, now); break;
        default:  client_touch(fd, now, false); break;
    }

    kfree(payload);
}

void ws_client_session_init(void)
{
    memset(s_clients, 0, sizeof(s_clients));
    INIT_LIST_HEAD(&s_client_list);
    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        INIT_LIST_HEAD(&s_clients[i].list);
        s_clients[i].fd = -1;
    }
    pthread_mutex_lock(&s_pending_mutex);
    s_pending_response[0] = '\0';
    s_has_pending = false;
    pthread_mutex_unlock(&s_pending_mutex);
}

int ws_client_session_update_fdset(fd_set *readfds, int maxfd)
{
    pthread_mutex_lock(&s_clients_mutex);
    ws_client_t *client;
    list_for_each_entry(client, &s_client_list, list, ws_client_t) {
        FD_SET(client->fd, readfds);
        if (client->fd > maxfd) maxfd = client->fd;
    }
    pthread_mutex_unlock(&s_clients_mutex);
    return maxfd;
}

void ws_client_session_handle_ready(const fd_set *readfds)
{
    pthread_mutex_lock(&s_clients_mutex);
    ws_client_t *client, *next;
    list_for_each_entry_safe(client, next, &s_client_list, list, ws_client_t) {
        if (FD_ISSET(client->fd, readfds)) {
            int fd = client->fd;
            pthread_mutex_unlock(&s_clients_mutex);
            handle_message(fd);
            pthread_mutex_lock(&s_clients_mutex);
        }
    }
    pthread_mutex_unlock(&s_clients_mutex);
}

void ws_client_session_keepalive_tick(void)
{
    time_t now = time(NULL);
    int ping_fds[WS_MAX_CLIENTS];
    int stale_fds[WS_MAX_CLIENTS];
    int ping_count = 0;
    int stale_count = 0;

    pthread_mutex_lock(&s_clients_mutex);
    ws_client_t *client;
    list_for_each_entry(client, &s_client_list, list, ws_client_t) {

        if (client->awaiting_pong && (now - client->last_pong) > WS_PONG_TIMEOUT) {
            stale_fds[stale_count++] = client->fd;
            continue;
        }

        if ((now - client->last_seen) >= WS_PING_INTERVAL
            && (now - client->last_ping) >= WS_PING_INTERVAL) {
            client->last_ping = now;
            client->awaiting_pong = true;
            ping_fds[ping_count++] = client->fd;
        }
    }
    pthread_mutex_unlock(&s_clients_mutex);

    for (int i = 0; i < ping_count; i++) {
        if (ws_send_ping(ping_fds[i]) != 0) {
            remove_client(ping_fds[i]);
        }
    }

    for (int i = 0; i < stale_count; i++) {
        remove_client(stale_fds[i]);
    }
}
