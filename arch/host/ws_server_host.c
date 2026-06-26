/* WebSocket 服务器——TCP 监听、HTTP/WSS 分离、WS 握手、消息分发。
 *
 * 架构概览：
 * 单一 TCP 端口同时服务 HTTP 和 WebSocket 客户端。
 * 连接建立后读取首个请求行，检测 "Upgrade: websocket" 头：
 *   - 有升级头 → 执行 WS 握手（RFC 6455 Sec-WebSocket-Accept），升级后由 ws_client 管理
 *   - 无升级头 → 交由 ws_http_helpers.c 的 HTTP 路由处理
 *
 * WebSocket 握手流程（RFC 6455）：
 *   1. 客户端发送 Sec-WebSocket-Key（随机 16 字节 Base64）
 *   2. 服务器拼接 Key + WS_GUID（"258EAFA5-E914-47DA-95CA-C5AB0DC85B11"）
 *   3. SHA1 哈希 → Base64 编码 → 作为 Sec-WebSocket-Accept 返回
 *   4. 返回 HTTP 101 Switching Protocols，升级完成
 *
 * 消息帧协议：
 *   - 文本帧：opcode 0x1，JSON 格式消息
 *   - 二进制帧：opcode 0x2
 *   - 关闭帧：opcode 0x8
 *   - Ping/Pong：opcode 0x9/0xA，用于连接保活
 *   - 客户端→服务器帧必须 MASK，服务器→客户端不 MASK
 *
 * 消息类型（JSON "type" 字段）：
 *   - "response"     : Agent 回复文本
 *   - "reasoning"    : 思维链输出
 *   - "tool"         : 工具执行事件
 *   - "sudo_request" : 请求 sudo 密码
 *   - pet 相关类型：PET_WS_TYPE_RESPONSE 等
 */

#include "drivers/channel/gateway/ws_server.h"
#include "drivers/channel/gateway/ws_client.h"
#include "drivers/channel/gateway/ws_http_helpers.h"
#include "runtime.h"
#include "drivers/pet/pet_event.h"
#include "autoconf.h"

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

#include "linux/printk.h"
#include "cjson.h"

/* Web UI 静态文件缺失时的内置降级 HTML 页面 */
static const char *UI_FALLBACK_HTML =
    "<!doctype html>\n"
    "<html lang=\"en\">\n"
    "<head>\n"
    "  <meta charset=\"utf-8\" />\n"
    "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\" />\n"
    "  <title>Agent</title>\n"
    "</head>\n"
    "<body>\n"
    "  <h1>Agent</h1>\n"
    "  <p>Web UI assets are missing. Please make sure index.html, app.css, and app.js exist under the Agent web data directory.</p>\n"
    "</body>\n"
    "</html>\n";

static int s_server_fd = -1;          /* 服务器监听 socket */
static pthread_t s_server_thread;     /* 服务器线程句柄 */
static bool s_running = false;        /* 运行状态标志 */
static int s_server_port = 1234;      /* 监听端口（从 config.json 读取） */

/* RFC 6455 定义的 WebSocket 魔术字符串 GUID */
static const char *WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

/* OpenSSL Base64 编码封装。 */
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

/* 配置客户端 socket：SO_KEEPALIVE + TCP keepalive 参数（30s idle, 10s interval, 3 retries）。 */
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

/**
 * WebSocket 握手——RFC 6455 规范实现。
 * 1. 从 HTTP 请求中提取 Sec-WebSocket-Key
 * 2. 拼接 WS_GUID 后 SHA1 哈希
 * 3. Base64 编码作为 Sec-WebSocket-Accept
 * 4. 返回 HTTP 101 Switching Protocols 响应
 * @return 0 成功，-1 失败
 */
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

/**
 * 连接分派器：读取首个请求行，判断是 WebSocket 升级还是普通 HTTP。
 * @return 1=WebSocket（fd 已升级，交由 ws_client 管理），
 *          0=HTTP（fd 已在路由中关闭），
 *         -1=错误
 */
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

/**
 * 服务器主循环（独立线程）。
 * 使用 select() 同时监听新的 TCP 连接和已有 WS 客户端事件。
 * 每 1 秒触发一次 keepalive 检查（ping/pong 保活）。
 */
static void *ws_server_loop(void *arg)
{
    (void)arg;
    pr_info("WebSocket server started on port %d", s_server_port);

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

/* 启动 WebSocket 服务器：创建 TCP socket + bind + listen + 启动工作线程。 */
err_t ws_server_start(void)
{
    ws_client_session_init();
    s_server_port = runtime_config_get_web_port();

    s_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (s_server_fd < 0) {
        pr_err("Failed to create socket (errno=%d: %s)", errno, strerror(errno));
        return ERR_FAIL;
    }

    int opt = 1;
    setsockopt(s_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)s_server_port);

    if (bind(s_server_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        pr_err("Failed to bind port %d", s_server_port);
        close(s_server_fd);
        s_server_fd = -1;
        return ERR_FAIL;
    }

    if (listen(s_server_fd, WS_MAX_CLIENTS) != 0) {
        pr_err("Failed to listen");
        close(s_server_fd);
        s_server_fd = -1;
        return ERR_FAIL;
    }

    s_running = true;
    if (pthread_create(&s_server_thread, NULL, ws_server_loop, NULL) != 0) {
        s_running = false;
        close(s_server_fd);
        s_server_fd = -1;
        return ERR_FAIL;
    }
    pthread_detach(s_server_thread);
    return 0;
}

/* 向指定 chat_id 发送响应文本（无 reasoning）。 */
err_t ws_server_send(const char *chat_id, const char *text)
{
    return ws_server_send_with_reasoning(chat_id, text, NULL);
}

/* 发送回复文本，可附带 reasoning（思维链）。reasoning 先独立发送，然后发送正文。 */
err_t ws_server_send_with_reasoning(const char *chat_id, const char *text, const char *reasoning)
{
    if (reasoning && reasoning[0]) {
        cJSON *reas = cJSON_CreateObject();
        cJSON_AddStringToObject(reas, "type", "reasoning");
        cJSON_AddStringToObject(reas, "content", reasoning);
        cJSON_AddStringToObject(reas, "chat_id", chat_id);
        ws_client_session_send_json(chat_id, reas);
        cJSON_Delete(reas);
    }

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "type", "response");
    cJSON_AddStringToObject(resp, "content", text);
    cJSON_AddStringToObject(resp, "chat_id", chat_id);
    err_t err = ws_client_session_send_json(chat_id, resp);
    cJSON_Delete(resp);
    return err;
}

/* 发送工具执行事件通知（type="tool"）。 */
err_t ws_server_send_tool_event(const char *chat_id, const char *text)
{
    cJSON *resp = cJSON_CreateObject();
    if (!resp) {
        return ERR_NO_MEM;
    }
    cJSON_AddStringToObject(resp, "type", "tool");
    cJSON_AddStringToObject(resp, "content", text ? text : "");
    cJSON_AddStringToObject(resp, "chat_id", chat_id ? chat_id : "");
    err_t err = ws_client_session_send_json_quiet(chat_id, resp);
    cJSON_Delete(resp);
    return err;
}

err_t ws_server_send_subagent_event(const char *chat_id,
                                    const char *event_type,
                                    const char *subagent_type,
                                    const char *task,
                                    const char *detail)
{
    cJSON *resp = cJSON_CreateObject();
    if (!resp) {
        return ERR_NO_MEM;
    }
    cJSON_AddStringToObject(resp, "type", event_type ? event_type : "subagent_progress");
    cJSON_AddStringToObject(resp, "chat_id", chat_id ? chat_id : "");
    cJSON_AddStringToObject(resp, "subagent_type", subagent_type ? subagent_type : "");
    cJSON_AddStringToObject(resp, "task", task ? task : "");
    cJSON_AddStringToObject(resp, "detail", detail ? detail : "");
    err_t err = ws_client_session_send_json_quiet(chat_id, resp);
    cJSON_Delete(resp);
    return err;
}

static err_t ws_server_send_coordinator_message(const char *chat_id,
                                                const char *type,
                                                const char *json_agents)
{
    cJSON *resp = cJSON_CreateObject();
    cJSON *agents = cJSON_Parse(json_agents ? json_agents : "[]");
    if (!resp || !agents || !cJSON_IsArray(agents)) {
        cJSON_Delete(resp);
        cJSON_Delete(agents);
        return ERR_INVALID_ARG;
    }
    cJSON_AddStringToObject(resp, "type", type);
    cJSON_AddStringToObject(resp, "chat_id", chat_id ? chat_id : "");
    cJSON_AddItemToObject(resp, "agents", agents);
    err_t err = ws_client_session_send_json_quiet(chat_id, resp);
    cJSON_Delete(resp);
    return err;
}

err_t ws_server_send_coordinator_status(const char *chat_id, const char *json_agents)
{
    return ws_server_send_coordinator_message(chat_id, "coordinator_status", json_agents);
}

err_t ws_server_send_coordinator_output(const char *chat_id, const char *json_agents)
{
    return ws_server_send_coordinator_message(chat_id, "coordinator_output", json_agents);
}

err_t ws_server_send_coordinator_done(const char *chat_id, const char *json_payload)
{
    cJSON *resp = cJSON_CreateObject();
    cJSON *payload = cJSON_Parse(json_payload ? json_payload : "{}");
    if (!resp || !payload || !cJSON_IsObject(payload)) {
        cJSON_Delete(resp);
        cJSON_Delete(payload);
        return ERR_INVALID_ARG;
    }
    cJSON_AddStringToObject(resp, "type", "coordinator_done");
    cJSON_AddStringToObject(resp, "chat_id", chat_id ? chat_id : "");
    cJSON_AddItemToObject(resp, "coordinator", payload);
    err_t err = ws_client_session_send_json_quiet(chat_id, resp);
    cJSON_Delete(resp);
    return err;
}

/* 向宠物会话发送响应（type=PET_WS_TYPE_RESPONSE，通过 pet_chat_id 路由到对应 WS 客户端）。 */
err_t ws_server_send_pet_response(const char *pet_chat_id, const char *text)
{
    char ws_chat_id[64];
    if (!pet_chat_id_to_ws_chat_id(pet_chat_id, ws_chat_id, sizeof(ws_chat_id))) {
        return ERR_INVALID_ARG;
    }

    cJSON *resp = cJSON_CreateObject();
    if (!resp) {
        return ERR_NO_MEM;
    }
    cJSON_AddStringToObject(resp, "type", PET_WS_TYPE_RESPONSE);
    cJSON_AddStringToObject(resp, "content", text ? text : "");
    cJSON_AddStringToObject(resp, "chat_id", pet_chat_id ? pet_chat_id : "");
    err_t err = ws_client_session_send_json(ws_chat_id, resp);
    cJSON_Delete(resp);
    return err;
}

/* 向 Web 客户端发送 sudo 密码请求（type="sudo_request"）。 */
err_t ws_server_send_sudo_request(const char *chat_id, const char *request_id, const char *prompt_text)
{
    cJSON *resp = cJSON_CreateObject();
    if (!resp) {
        return ERR_NO_MEM;
    }
    cJSON_AddStringToObject(resp, "type", "sudo_request");
    cJSON_AddStringToObject(resp, "chat_id", chat_id ? chat_id : "");
    cJSON_AddStringToObject(resp, "request_id", request_id ? request_id : "");
    cJSON_AddStringToObject(resp, "prompt", prompt_text ? prompt_text : "Please enter your sudo password.");
    err_t err = ws_client_session_send_json(chat_id, resp);
    cJSON_Delete(resp);
    return err;
}

err_t ws_server_stop(void)
{
    s_running = false;
    if (s_server_fd >= 0) {
        close(s_server_fd);
        s_server_fd = -1;
    }
    return 0;
}
