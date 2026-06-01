/* WebSocket 客户端会话：连接表、消息处理、keepalive。 */

#include "gateway/ws_client_session.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "bus/message_bus.h"
#include "gateway/ws_server.h"
#include "pet/pet_event.h"
#include "daima_config.h"
#include "daima_log.h"
#include "cJSON.h"

static const char *TAG = "ws";

#define WS_PING_INTERVAL 20
#define WS_PONG_TIMEOUT  60

typedef struct {
    int fd;
    char chat_id[32];
    bool active;
    time_t last_seen;
    time_t last_ping;
    time_t last_pong;
    bool awaiting_pong;
} ws_client_t;

static ws_client_t s_clients[DAIMA_WS_MAX_CLIENTS];
static pthread_mutex_t s_clients_mutex = PTHREAD_MUTEX_INITIALIZER;

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

static ws_client_t *find_client_by_fd(int fd)
{
    for (int i = 0; i < DAIMA_WS_MAX_CLIENTS; i++) {
        if (s_clients[i].active && s_clients[i].fd == fd) {
            return &s_clients[i];
        }
    }
    return NULL;
}

static ws_client_t *find_client_by_chat_id(const char *chat_id)
{
    for (int i = 0; i < DAIMA_WS_MAX_CLIENTS; i++) {
        if (s_clients[i].active && strcmp(s_clients[i].chat_id, chat_id) == 0) {
            return &s_clients[i];
        }
    }
    return NULL;
}

static void remove_client(int fd)
{
    pthread_mutex_lock(&s_clients_mutex);
    for (int i = 0; i < DAIMA_WS_MAX_CLIENTS; i++) {
        if (s_clients[i].active && s_clients[i].fd == fd) {
            DAIMA_LOGI(TAG, "Client disconnected: %s", s_clients[i].chat_id);
            s_clients[i].active = false;
            close(s_clients[i].fd);
            s_clients[i].fd = -1;
            break;
        }
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
    for (int i = 0; i < DAIMA_WS_MAX_CLIENTS; i++) {
        if (s_clients[i].active && s_clients[i].fd != keep_fd
            && strcmp(s_clients[i].chat_id, chat_id) == 0) {
            int old_fd = s_clients[i].fd;
            s_clients[i].active = false;
            s_clients[i].fd = -1;
            s_clients[i].chat_id[0] = '\0';
            pthread_mutex_unlock(&s_clients_mutex);
            if (old_fd >= 0) close(old_fd);
            DAIMA_LOGW(TAG, "Dropped duplicate chat_id=%s (fd=%d)", chat_id, old_fd);
            pthread_mutex_lock(&s_clients_mutex);
        }
    }
    pthread_mutex_unlock(&s_clients_mutex);
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
        DAIMA_LOGW(TAG, "No WS client with chat_id=%s", chat_id);
        return DAIMA_ERR_NOT_FOUND;
    }
    int fd = client->fd;
    pthread_mutex_unlock(&s_clients_mutex);

    char *json_str = cJSON_PrintUnformatted(obj);
    if (!json_str) {
        return DAIMA_ERR_NO_MEM;
    }
    int ret = ws_send_text(fd, json_str);
    free(json_str);
    if (ret != 0) {
        DAIMA_LOGW(TAG, "Failed to send JSON to %s", chat_id);
        remove_client(fd);
        return DAIMA_FAIL;
    }
    return DAIMA_OK;
}

bool ws_client_session_add(int fd)
{
    time_t now = time(NULL);
    int slot = -1;
    int evict_idx = -1;
    time_t max_idle = 0;

    pthread_mutex_lock(&s_clients_mutex);
    for (int i = 0; i < DAIMA_WS_MAX_CLIENTS; i++) {
        if (!s_clients[i].active) {
            slot = i;
            break;
        }
        if (s_clients[i].active) {
            time_t idle = now - s_clients[i].last_seen;
            if (idle > WS_PONG_TIMEOUT && idle > max_idle) {
                max_idle = idle;
                evict_idx = i;
            }
        }
    }

    if (slot < 0 && evict_idx >= 0) {
        int old_fd = s_clients[evict_idx].fd;
        char old_chat[32];
        snprintf(old_chat, sizeof(old_chat), "%s", s_clients[evict_idx].chat_id);
        s_clients[evict_idx].active = false;
        s_clients[evict_idx].fd = -1;
        s_clients[evict_idx].chat_id[0] = '\0';
        slot = evict_idx;
        pthread_mutex_unlock(&s_clients_mutex);
        if (old_fd >= 0) close(old_fd);
        DAIMA_LOGW(TAG, "Evicted stale client %s (fd=%d)", old_chat, old_fd);
        pthread_mutex_lock(&s_clients_mutex);
    }

    if (slot >= 0) {
        s_clients[slot].fd = fd;
        snprintf(s_clients[slot].chat_id, sizeof(s_clients[slot].chat_id), "ws_%d", fd);
        s_clients[slot].last_seen = now;
        s_clients[slot].last_pong = now;
        s_clients[slot].last_ping = now;
        s_clients[slot].awaiting_pong = false;
        s_clients[slot].active = true;
        pthread_mutex_unlock(&s_clients_mutex);
        DAIMA_LOGI(TAG, "Client connected: %s (fd=%d)", s_clients[slot].chat_id, fd);
        return true;
    }

    pthread_mutex_unlock(&s_clients_mutex);
    DAIMA_LOGW(TAG, "Max clients reached, rejecting fd=%d", fd);
    return false;
}

static void handle_message(int fd)
{
    unsigned char hdr[2];
    if (recv_all(fd, hdr, 2) != 0) {
        remove_client(fd);
        return;
    }

    unsigned char opcode = hdr[0] & 0x0F;
    bool masked = (hdr[1] & 0x80) != 0;
    uint64_t len = hdr[1] & 0x7F;

    if (len == 126) {
        unsigned char ext[2];
        if (recv_all(fd, ext, 2) != 0) { remove_client(fd); return; }
        len = ((uint64_t)ext[0] << 8) | ext[1];
    } else if (len == 127) {
        unsigned char ext[8];
        if (recv_all(fd, ext, 8) != 0) { remove_client(fd); return; }
        len = 0;
        for (int i = 0; i < 8; i++) {
            len = (len << 8) | ext[i];
        }
    }

    unsigned char mask[4] = {0};
    if (masked) {
        if (recv_all(fd, mask, 4) != 0) { remove_client(fd); return; }
    }

    if (len > 65536) { remove_client(fd); return; }

    char *payload = calloc(1, len + 1);
    if (!payload) { remove_client(fd); return; }
    if (recv_all(fd, payload, len) != 0) {
        free(payload);
        remove_client(fd);
        return;
    }

    if (masked) {
        for (uint64_t i = 0; i < len; i++) {
            payload[i] ^= mask[i % 4];
        }
    }
    payload[len] = '\0';

    time_t now = time(NULL);

    if (opcode == 0x8) {
        free(payload);
        remove_client(fd);
        return;
    }

    if (opcode == 0x9) {
        ws_send_pong(fd, payload, len);
        client_touch(fd, now, true);
        free(payload);
        return;
    }

    if (opcode == 0xA) {
        client_touch(fd, now, true);
        free(payload);
        return;
    }

    if (opcode != 0x1) {
        client_touch(fd, now, false);
        free(payload);
        return;
    }

    client_touch(fd, now, false);

    ws_client_t *client = find_client_by_fd(fd);

    cJSON *root = cJSON_Parse(payload);
    free(payload);
    if (!root) {
        DAIMA_LOGW(TAG, "Invalid JSON from fd=%d", fd);
        return;
    }

    cJSON *type = cJSON_GetObjectItem(root, "type");
    cJSON *content = cJSON_GetObjectItem(root, "content");

    if (type && cJSON_IsString(type) && strcmp(type->valuestring, "ping") == 0) {
        ws_send_text(fd, "{\"type\":\"pong\"}");
        cJSON_Delete(root);
        return;
    }

    if (type && cJSON_IsString(type) && strcmp(type->valuestring, "message") == 0
        && content && cJSON_IsString(content)) {

        const char *chat_id = client ? client->chat_id : "ws_unknown";
        cJSON *cid = cJSON_GetObjectItem(root, "chat_id");
        if (cid && cJSON_IsString(cid)) {
            chat_id = cid->valuestring;
            drop_duplicate_chat_id(chat_id, fd);
            if (client) {
                strncpy(client->chat_id, chat_id, sizeof(client->chat_id) - 1);
            }
        }

        DAIMA_LOGI(TAG, "WS message from %s: %.40s...", chat_id, content->valuestring);

        daima_msg_t msg = {0};
        strncpy(msg.channel, DAIMA_CHAN_WEBSOCKET, sizeof(msg.channel) - 1);
        strncpy(msg.chat_id, chat_id, sizeof(msg.chat_id) - 1);
        strncpy(msg.source, DAIMA_MSG_SOURCE_USER, sizeof(msg.source) - 1);
        msg.content = strdup(content->valuestring);
        if (msg.content) {
            message_bus_push_inbound(&msg);
        }
    }

    if (type && cJSON_IsString(type) && strcmp(type->valuestring, PET_WS_TYPE_ACTION) == 0) {
        const char *chat_id = client ? client->chat_id : "ws_unknown";
        const char *action = NULL;
        const char *pet_id = NULL;
        char pet_chat_id[64] = {0};

        cJSON *cid = cJSON_GetObjectItem(root, "chat_id");
        cJSON *pet_cid = cJSON_GetObjectItem(root, "pet_chat_id");
        cJSON *action_json = cJSON_GetObjectItem(root, "action");
        cJSON *pet_id_json = cJSON_GetObjectItem(root, "pet_id");

        if (cid && cJSON_IsString(cid) && cid->valuestring[0]) {
            chat_id = cid->valuestring;
            drop_duplicate_chat_id(chat_id, fd);
            if (client) {
                strncpy(client->chat_id, chat_id, sizeof(client->chat_id) - 1);
            }
        }

        if (pet_cid && cJSON_IsString(pet_cid) && pet_cid->valuestring[0]) {
            snprintf(pet_chat_id, sizeof(pet_chat_id), "%s", pet_cid->valuestring);
        } else {
            if (!pet_build_chat_id(chat_id, pet_chat_id, sizeof(pet_chat_id))) {
                cJSON_Delete(root);
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
            daima_msg_t msg = {0};
            strncpy(msg.channel, DAIMA_CHAN_PET, sizeof(msg.channel) - 1);
            strncpy(msg.chat_id, pet_chat_id, sizeof(msg.chat_id) - 1);
            strncpy(msg.source, DAIMA_MSG_SOURCE_USER, sizeof(msg.source) - 1);
            msg.content = pet_prompt;
            message_bus_push_inbound(&msg);
        }
    }

    if (type && cJSON_IsString(type) && strcmp(type->valuestring, "sudo_password") == 0) {
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
            char *payload2 = calloc(1, need);
            if (payload2) {
                snprintf(payload2, need, "__sudo_password__:%s:%s:%d",
                         req->valuestring,
                         pwd->valuestring,
                         (cancelled && cJSON_IsTrue(cancelled)) ? 1 : 0);
                daima_msg_t msg = {0};
                strncpy(msg.channel, DAIMA_CHAN_WEBSOCKET, sizeof(msg.channel) - 1);
                strncpy(msg.chat_id, chat_id, sizeof(msg.chat_id) - 1);
                strncpy(msg.source, DAIMA_MSG_SOURCE_INTERNAL, sizeof(msg.source) - 1);
                msg.content = payload2;
                message_bus_push_inbound(&msg);
            }
        }
    }

    cJSON_Delete(root);
}

void ws_client_session_init(void)
{
    memset(s_clients, 0, sizeof(s_clients));
}

int ws_client_session_update_fdset(fd_set *readfds, int maxfd)
{
    pthread_mutex_lock(&s_clients_mutex);
    for (int i = 0; i < DAIMA_WS_MAX_CLIENTS; i++) {
        if (s_clients[i].active) {
            FD_SET(s_clients[i].fd, readfds);
            if (s_clients[i].fd > maxfd) maxfd = s_clients[i].fd;
        }
    }
    pthread_mutex_unlock(&s_clients_mutex);
    return maxfd;
}

void ws_client_session_handle_ready(const fd_set *readfds)
{
    pthread_mutex_lock(&s_clients_mutex);
    for (int i = 0; i < DAIMA_WS_MAX_CLIENTS; i++) {
        if (s_clients[i].active && FD_ISSET(s_clients[i].fd, readfds)) {
            int fd = s_clients[i].fd;
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
    int ping_fds[DAIMA_WS_MAX_CLIENTS];
    int stale_fds[DAIMA_WS_MAX_CLIENTS];
    int ping_count = 0;
    int stale_count = 0;

    pthread_mutex_lock(&s_clients_mutex);
    for (int i = 0; i < DAIMA_WS_MAX_CLIENTS; i++) {
        if (!s_clients[i].active) continue;

        if (s_clients[i].awaiting_pong && (now - s_clients[i].last_pong) > WS_PONG_TIMEOUT) {
            stale_fds[stale_count++] = s_clients[i].fd;
            continue;
        }

        if ((now - s_clients[i].last_seen) >= WS_PING_INTERVAL
            && (now - s_clients[i].last_ping) >= WS_PING_INTERVAL) {
            s_clients[i].last_ping = now;
            s_clients[i].awaiting_pong = true;
            ping_fds[ping_count++] = s_clients[i].fd;
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
