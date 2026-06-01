#include "gateway/ws_server.h"
#include "gateway/ws_client_session.h"
#include "gateway/ws_http_helpers.h"
#include "app/runtime_config.h"
#include "pet/pet_event.h"
#include "daima_config.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <openssl/evp.h>
#include <openssl/sha.h>

#include "daima_log.h"
#include "cJSON.h"

static const char *TAG = "ws";

static const char *UI_FALLBACK_HTML =
    "<!doctype html>\n"
    "<html lang=\"en\">\n"
    "<head>\n"
    "  <meta charset=\"utf-8\" />\n"
    "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\" />\n"
    "  <title>代马 Daima</title>\n"
    "</head>\n"
    "<body>\n"
    "  <h1>代马 Daima</h1>\n"
    "  <p>Web UI assets are missing. Please make sure index.html, app.css, and app.js exist under the Daima web data directory.</p>\n"
    "</body>\n"
    "</html>\n";

static int s_server_fd = -1;
static pthread_t s_server_thread;
static bool s_running = false;
static int s_server_port = 1234;

static const char *WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

static void base64_encode(const unsigned char *in, size_t in_len, char *out, size_t out_len)
{
    int needed = 4 * ((int)in_len + 2) / 3 + 1;
    if ((int)out_len < needed) {
        out[0] = '\0';
        return;
    }
    int n = EVP_EncodeBlock((unsigned char *)out, in, (int)in_len);
    if (n < 0) {
        out[0] = '\0';
    } else {
        out[n] = '\0';
    }
}

static void configure_client_socket(int fd)
{
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt));
#ifdef TCP_KEEPIDLE
    int idle = 30;
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
#endif
#ifdef TCP_KEEPINTVL
    int intvl = 10;
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl));
#endif
#ifdef TCP_KEEPCNT
    int cnt = 3;
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt));
#endif
}

static int ws_handshake_from_request(int client_fd, const char *req)
{
    const char *key_hdr = "Sec-WebSocket-Key:";
    char *key_pos = strcasestr((char *)req, key_hdr);
    if (!key_pos) return -1;
    key_pos += strlen(key_hdr);
    while (*key_pos == ' ') key_pos++;

    char key[128];
    size_t i = 0;
    while (*key_pos && *key_pos != '\r' && *key_pos != '\n' && i < sizeof(key) - 1) {
        key[i++] = *key_pos++;
    }
    key[i] = '\0';

    char accept_src[256];
    snprintf(accept_src, sizeof(accept_src), "%s%s", key, WS_GUID);

    unsigned char sha1[SHA_DIGEST_LENGTH];
    SHA1((unsigned char *)accept_src, strlen(accept_src), sha1);

    char accept_b64[64];
    base64_encode(sha1, SHA_DIGEST_LENGTH, accept_b64, sizeof(accept_b64));

    char resp[256];
    int len = snprintf(resp, sizeof(resp),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n\r\n",
        accept_b64);

    if (send(client_fd, resp, len, 0) != len) return -1;
    return 0;
}

static int handle_http_or_ws(int client_fd)
{
    char req[4096];
    ssize_t n = recv(client_fd, req, sizeof(req) - 1, 0);
    if (n <= 0) return -1;
    req[n] = '\0';

    if (strcasestr(req, "Upgrade: websocket")
        || strcasestr(req, "Sec-WebSocket-Key:")) {
        if (ws_handshake_from_request(client_fd, req) == 0) return 1;
        return -1;
    }
    return ws_http_handle_request(client_fd, req, UI_FALLBACK_HTML);
}

static void *ws_server_loop(void *arg)
{
    (void)arg;
    DAIMA_LOGI(TAG, "WebSocket server started on port %d", s_server_port);

    while (s_running) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(s_server_fd, &readfds);
        int maxfd = s_server_fd;

        maxfd = ws_client_session_update_fdset(&readfds, maxfd);

        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
        int ready = select(maxfd + 1, &readfds, NULL, NULL, &tv);
        if (ready < 0) {
            if (errno == EINTR) continue;
            break;
        }

        if (ready > 0) {
            if (FD_ISSET(s_server_fd, &readfds)) {
                struct sockaddr_in addr;
                socklen_t len = sizeof(addr);
            int client_fd = accept(s_server_fd, (struct sockaddr *)&addr, &len);
            if (client_fd >= 0) {
                configure_client_socket(client_fd);
                int rc = handle_http_or_ws(client_fd);
                if (rc == 1) {
                    if (!ws_client_session_add(client_fd)) close(client_fd);
                } else {
                        close(client_fd);
                    }
                }
            }

            ws_client_session_handle_ready(&readfds);
        }

        ws_client_session_keepalive_tick();
    }

    return NULL;
}

daima_err_t ws_server_start(void)
{
    ws_client_session_init();
    s_server_port = runtime_config_get_web_port();

    s_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (s_server_fd < 0) {
        DAIMA_LOGE(TAG, "Failed to create socket (errno=%d: %s)", errno, strerror(errno));
        return DAIMA_FAIL;
    }

    int opt = 1;
    setsockopt(s_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)s_server_port);

    if (bind(s_server_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        DAIMA_LOGE(TAG, "Failed to bind port %d", s_server_port);
        close(s_server_fd);
        s_server_fd = -1;
        return DAIMA_FAIL;
    }

    if (listen(s_server_fd, DAIMA_WS_MAX_CLIENTS) != 0) {
        DAIMA_LOGE(TAG, "Failed to listen");
        close(s_server_fd);
        s_server_fd = -1;
        return DAIMA_FAIL;
    }

    s_running = true;
    if (pthread_create(&s_server_thread, NULL, ws_server_loop, NULL) != 0) {
        s_running = false;
        close(s_server_fd);
        s_server_fd = -1;
        return DAIMA_FAIL;
    }
    pthread_detach(s_server_thread);
    return DAIMA_OK;
}

daima_err_t ws_server_send(const char *chat_id, const char *text)
{
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "type", "response");
    cJSON_AddStringToObject(resp, "content", text);
    cJSON_AddStringToObject(resp, "chat_id", chat_id);
    daima_err_t err = ws_client_session_send_json(chat_id, resp);
    cJSON_Delete(resp);
    return err;
}

daima_err_t ws_server_send_tool_event(const char *chat_id, const char *text)
{
    cJSON *resp = cJSON_CreateObject();
    if (!resp) {
        return DAIMA_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(resp, "type", "tool");
    cJSON_AddStringToObject(resp, "content", text ? text : "");
    cJSON_AddStringToObject(resp, "chat_id", chat_id ? chat_id : "");
    daima_err_t err = ws_client_session_send_json(chat_id, resp);
    cJSON_Delete(resp);
    return err;
}

daima_err_t ws_server_send_pet_response(const char *pet_chat_id, const char *text)
{
    char ws_chat_id[64];
    if (!pet_chat_id_to_ws_chat_id(pet_chat_id, ws_chat_id, sizeof(ws_chat_id))) {
        return DAIMA_ERR_INVALID_ARG;
    }

    cJSON *resp = cJSON_CreateObject();
    if (!resp) {
        return DAIMA_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(resp, "type", PET_WS_TYPE_RESPONSE);
    cJSON_AddStringToObject(resp, "content", text ? text : "");
    cJSON_AddStringToObject(resp, "chat_id", pet_chat_id ? pet_chat_id : "");
    daima_err_t err = ws_client_session_send_json(ws_chat_id, resp);
    cJSON_Delete(resp);
    return err;
}

daima_err_t ws_server_send_sudo_request(const char *chat_id, const char *request_id, const char *prompt_text)
{
    cJSON *resp = cJSON_CreateObject();
    if (!resp) {
        return DAIMA_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(resp, "type", "sudo_request");
    cJSON_AddStringToObject(resp, "chat_id", chat_id ? chat_id : "");
    cJSON_AddStringToObject(resp, "request_id", request_id ? request_id : "");
    cJSON_AddStringToObject(resp, "prompt", prompt_text ? prompt_text : "Please enter your sudo password.");
    daima_err_t err = ws_client_session_send_json(chat_id, resp);
    cJSON_Delete(resp);
    return err;
}

daima_err_t ws_server_stop(void)
{
    s_running = false;
    if (s_server_fd >= 0) {
        close(s_server_fd);
        s_server_fd = -1;
    }
    return DAIMA_OK;
}
