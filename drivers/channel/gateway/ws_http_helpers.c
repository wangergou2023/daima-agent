/* WebSocket HTTP 辅助函数——HTTP 请求路由与静态资源服务。
 *
 * 架构说明：
 * WebSocket 服务器同时处理 HTTP 和 WS 连接。客户端首先发送 HTTP 请求，
 * 服务器检查 Upgrade 头：若包含 "websocket" 则执行 WS 握手升级，
 * 否则交由本文件的 HTTP 路由处理。
 *
 * 路由设计（ws_http_handle_request）：
 *   GET  /                  → index.html（静态文件，无时使用内置降级 HTML）
 *   GET  /app.css           → 样式表
 *   GET  /app.js            → 前端 JS
 *   GET  /pet.js            → 电子宠物 JS
 *   GET  /pets/...          → 宠物资源文件（二进制，8MB 上限）
 *   GET  /health            → 健康检查
 *   GET  /api/context_stats → 上下文统计（tokens/使用率）
 *   GET  /api/sessions      → 会话列表
 *   GET  /api/session_history → 会话历史（原始消息）
 *   GET  /api/ui_config     → UI 配置（宠物包列表、终端安全级别）
 *   POST /api/session_delete → 删除会话
 *   POST /api/terminal_security → 设置终端安全级别
 *
 * WebSocket 升级握手流程（在 ws_server_host.c 中执行）：
 *   1. 客户端发送包含 Sec-WebSocket-Key 的 HTTP 请求
 *   2. 服务器解析 Key，拼接 WS_GUID 后 SHA1 哈希
 *   3. Base64 编码后以 Sec-WebSocket-Accept 头返回 101 响应
 *   4. 后续通信使用 WS 帧协议（opcode: text/binary/ping/pong/close）
 *
 * WebSocket 帧协议要点：
 *   - 帧头 2-10 字节（FIN/RSV/opcode/mask/payload_len）
 *   - 客户端→服务器帧必须 MASK，服务器→客户端帧不 MASK
 *   - opcode 0x1=文本帧, 0x2=二进制帧, 0x8=关闭帧, 0x9=ping, 0xA=pong
 *   - ping/pong 用于保活检测，双方收到后应立即回复
 */

#include "drivers/channel/gateway/ws_http_helpers.h"

#include "paths.h"
#include "runtime.h"
#include "context_build.h"
#include "delegate/delegate_state_json.h"
#include "delegate/delegate_session_json.h"
#include "delegate/delegate_task_store.h"
#include "kernel/turn/turn_context.h"
#include "drivers/llm/llm_proxy.h"
#include "drivers/memory/session_store.h"
#include "autoconf.h"
#include "linux/printk.h"
#include "cjson.h"

#include <dirent.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <time.h>
#include "linux/slab.h"

/* 通过循环 send() 发送完整数据，处理部分发送和 EINTR。 */
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

/* 发送原始字节 HTTP 响应（含 Content-Length）。 */
static void http_send_response_bytes(int fd, const char *status,
                                     const char *content_type,
                                     const void *body, size_t body_len)
{
    char header[256];
    int n = snprintf(header, sizeof(header),
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "Cache-Control: no-store\r\n"
        "\r\n",
        status,
        content_type ? content_type : "application/octet-stream",
        body_len);
    if (n > 0) {
        send_all(fd, header, (size_t)n);
    }
    if (body && body_len > 0) {
        send_all(fd, body, body_len);
    }
}

/* 发送字符串 HTTP 响应（自动计算 Content-Length）。 */
static void http_send_response(int fd, const char *status,
                               const char *content_type, const char *body)
{
    size_t body_len = body ? strlen(body) : 0;
    http_send_response_bytes(fd, status,
                             content_type ? content_type : "text/plain; charset=utf-8",
                             body, body_len);
}

/* 读取文本文件全部内容（返回 kmalloc 的 '\0' 结尾字符串）。 */
static char *read_text_file(const char *path, size_t max_bytes)
{
    if (!path || !path[0] || max_bytes == 0) {
        return NULL;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }

    long size = ftell(f);
    if (size < 0 || (size_t)size > max_bytes) {
        fclose(f);
        return NULL;
    }

    rewind(f);
    char *buf = kzalloc((size_t)size + 1, GFP_KERNEL);
    if (!buf) {
        fclose(f);
        return NULL;
    }

    size_t n = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[n] = '\0';
    return buf;
}

/* 读取二进制文件（返回 kmalloc 缓冲区 + 实际大小）。 */
static unsigned char *read_binary_file(const char *path, size_t max_bytes, size_t *size_out)
{
    if (size_out) {
        *size_out = 0;
    }
    if (!path || !path[0] || max_bytes == 0) {
        return NULL;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }

    long size = ftell(f);
    if (size < 0 || (size_t)size > max_bytes) {
        fclose(f);
        return NULL;
    }

    rewind(f);
    unsigned char *buf = NULL;
    if (size == 0) {
        buf = kzalloc(1, GFP_KERNEL);
    } else {
        buf = kmalloc((size_t)size, GFP_KERNEL);
    }
    if (!buf) {
        fclose(f);
        return NULL;
    }

    size_t n = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if (n != (size_t)size) {
        kfree(buf);
        return NULL;
    }

    if (size_out) {
        *size_out = n;
    }
    return buf;
}

/* 尝试从磁盘读取静态文件发送，若文件不存在则使用内置降级内容。 */
static void http_send_static_file_or_fallback(
    int fd,
    const char *content_type,
    const char *path,
    const char *fallback_body)
{
    char *body = read_text_file(path, 256 * 1024);
    if (body) {
        http_send_response(fd, "200 OK", content_type, body);
        kfree(body);
        return;
    }

    pr_warn("Static asset missing, fallback to built-in body: %s", path ? path : "(null)");
    http_send_response(fd, "200 OK", content_type, fallback_body ? fallback_body : "");
}

static bool is_safe_web_asset_name(const char *name)
{
    if (!name || !name[0]) {
        return false;
    }
    if (strstr(name, "..") != NULL || strchr(name, '/') != NULL || strchr(name, '\\') != NULL) {
        return false;
    }

    for (const unsigned char *p = (const unsigned char *)name; *p; ++p) {
        if (!(isalnum(*p) || *p == '.' || *p == '-' || *p == '_')) {
            return false;
        }
    }
    return true;
}

/* 根据文件扩展名返回 MIME Content-Type。 */
static const char *content_type_for_path(const char *path)
{
    const char *ext = path ? strrchr(path, '.') : NULL;
    if (!ext) return "application/octet-stream";

    if (strcasecmp(ext, ".html") == 0) return "text/html; charset=utf-8";
    if (strcasecmp(ext, ".css") == 0) return "text/css; charset=utf-8";
    if (strcasecmp(ext, ".js") == 0) return "application/javascript; charset=utf-8";
    if (strcasecmp(ext, ".json") == 0) return "application/json; charset=utf-8";
    if (strcasecmp(ext, ".webp") == 0) return "image/webp";
    if (strcasecmp(ext, ".png") == 0) return "image/png";
    if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcasecmp(ext, ".svg") == 0) return "image/svg+xml";
    if (strcasecmp(ext, ".txt") == 0) return "text/plain; charset=utf-8";
    return "application/octet-stream";
}

/* 校验静态资源路径安全性：无 ".."、仅含字母数字/ /./_/-。 */
static bool is_safe_asset_relative_path(const char *path)
{
    if (!path || !path[0] || strstr(path, "..") != NULL) {
        return false;
    }

    for (const unsigned char *p = (const unsigned char *)path; *p; ++p) {
        if (!(isalnum(*p) || *p == '/' || *p == '.' || *p == '_' || *p == '-')) {
            return false;
        }
    }
    return true;
}

/* 发送二进制文件（图片/字体等），自动检测 Content-Type。 */
static void http_send_binary_file(int fd, const char *path)
{
    size_t size = 0;
    unsigned char *body = read_binary_file(path, 8 * 1024 * 1024, &size);
    if (!body && size == 0) {
        http_send_response(fd, "404 Not Found",
                           "text/plain; charset=utf-8", "Not Found");
        return;
    }

    http_send_response_bytes(fd, "200 OK", content_type_for_path(path), body, size);
    kfree(body);
}

/* 解析 HTTP 请求行 "METHOD /path HTTP/version"。 */
static void parse_request_line(const char *req, char *method, size_t msz,
                               char *path, size_t psz)
{
    if (!req) return;
    const char *sp1 = strchr(req, ' ');
    if (!sp1) return;
    size_t mlen = (size_t)(sp1 - req);
    if (mlen >= msz) mlen = msz - 1;
    memcpy(method, req, mlen);
    method[mlen] = '\0';

    const char *sp2 = strchr(sp1 + 1, ' ');
    if (!sp2) return;
    size_t plen = (size_t)(sp2 - (sp1 + 1));
    if (plen >= psz) plen = psz - 1;
    memcpy(path, sp1 + 1, plen);
    path[plen] = '\0';
}

/* 从 URL 中分离 path 和 query string（原地修改 path）。 */
static void split_path_and_query(char *path, char **query_out)
{
    if (query_out) {
        *query_out = NULL;
    }
    if (!path) {
        return;
    }
    char *q = strchr(path, '?');
    if (!q) {
        return;
    }
    *q = '\0';
    if (query_out) {
        *query_out = q + 1;
    }
}

/* 从 URL query string 中提取指定 key 的值。 */
static void query_get_value(const char *query, const char *key, char *out, size_t out_sz)
{
    if (!out || out_sz == 0) return;
    out[0] = '\0';
    if (!query || !key || !key[0]) return;

    size_t key_len = strlen(key);
    const char *p = query;
    while (*p) {
        const char *amp = strchr(p, '&');
        size_t part_len = amp ? (size_t)(amp - p) : strlen(p);
        if (part_len > key_len + 1 && strncmp(p, key, key_len) == 0 && p[key_len] == '=') {
            size_t val_len = part_len - key_len - 1;
            if (val_len >= out_sz) val_len = out_sz - 1;
            memcpy(out, p + key_len + 1, val_len);
            out[val_len] = '\0';
            return;
        }
        if (!amp) break;
        p = amp + 1;
    }
}

static const char *http_request_body(const char *req)
{
    const char *marker = NULL;

    if (!req) {
        return NULL;
    }
    marker = strstr(req, "\r\n\r\n");
    if (!marker) {
        return NULL;
    }
    return marker + 4;
}

static bool json_body_get_string_field(const char *body,
                                       const char *key,
                                       char *out,
                                       size_t out_sz)
{
    cJSON *root = NULL;
    cJSON *item = NULL;
    const char *value = NULL;

    if (!out || out_sz == 0) {
        return false;
    }
    out[0] = '\0';
    if (!body || !body[0] || !key || !key[0]) {
        return false;
    }

    root = cJSON_Parse(body);
    if (!root) {
        return false;
    }

    item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsString(item) && item->valuestring && item->valuestring[0]) {
        value = item->valuestring;
        snprintf(out, out_sz, "%s", value);
    }

    cJSON_Delete(root);
    return out[0] != '\0';
}

/* 校验 chat_id 安全性：仅允许字母数字、下划线和连字符。 */
static bool is_safe_chat_id(const char *chat_id)
{
    if (!chat_id || !chat_id[0]) {
        return false;
    }
    for (const char *p = chat_id; *p; ++p) {
        if (!(isalnum((unsigned char)*p) || *p == '_' || *p == '-')) {
            return false;
        }
    }
    return true;
}

/* 校验终端安全级别：仅允许 "plan" 或 "build"。 */
static bool is_valid_terminal_security_level(const char *level)
{
    return level &&
           (strcmp(level, "plan") == 0 ||
            strcmp(level, "build") == 0);
}

/* 粗略估算 prompt token 数（字数 ÷ 4 + 固定开销）。 */
static int estimate_prompt_tokens_rough(const char *system_prompt, const cJSON *messages)
{
    size_t chars = system_prompt ? strlen(system_prompt) : 0;
    const cJSON *msg = NULL;
    cJSON_ArrayForEach(msg, messages) {
        cJSON *content = cJSON_GetObjectItem((cJSON *)msg, "content");
        if (content && cJSON_IsString(content) && content->valuestring) {
            chars += strlen(content->valuestring);
        }
        chars += 32;
    }
    return (int)(chars / 4) + 16;
}

/* 构建 /api/context_stats 响应：当前模型、token 用量、上下文使用率。 */
static char *build_context_stats_json(const char *chat_id)
{
    char history_json[LLM_STREAM_BUF_SIZE];
    char system_prompt[CONTEXT_BUF_SIZE];
    history_json[0] = '\0';
    system_prompt[0] = '\0';

    session_store_get_history_json(
        chat_id && chat_id[0] ? chat_id : "web_unknown",
        history_json,
        sizeof(history_json),
        AGENT_MAX_HISTORY);
    context_build_system_prompt(system_prompt, sizeof(system_prompt));

    cJSON *messages = cJSON_Parse(history_json);
    if (!messages) {
        messages = cJSON_CreateArray();
    }

    int used_tokens = estimate_prompt_tokens_rough(system_prompt, messages);
    int context_limit = llm_get_context_limit_tokens();
    const char *model = llm_get_model_name();

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "chat_id", chat_id && chat_id[0] ? chat_id : "web_unknown");
    cJSON_AddStringToObject(root, "model", model ? model : "unknown");
    cJSON_AddNumberToObject(root, "used_tokens", used_tokens);
    cJSON_AddNumberToObject(root, "context_limit_tokens", context_limit);
    if (context_limit > 0) {
        cJSON_AddNumberToObject(root, "usage_percent", (used_tokens * 100.0) / context_limit);
    } else {
        cJSON_AddNumberToObject(root, "usage_percent", 0);
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    cJSON_Delete(messages);
    return json;
}

/* 构建 /api/ui_config 响应：扫描 .codex-pet 目录作为宠物包列表，返回默认包和终端安全级别。 */
static char *build_ui_config_json(void)
{
    const char *default_pet_package_id = runtime_config_get_web_default_pet_package_id();
    DIR *dir = NULL;
    struct dirent *entry = NULL;

    cJSON *root = cJSON_CreateObject();
    cJSON *pet = cJSON_AddObjectToObject(root, "pet");
    cJSON *packages = NULL;
    if (!pet) {
        cJSON_Delete(root);
        return NULL;
    }

    packages = cJSON_AddArrayToObject(pet, "packages");
    if (!packages) {
        cJSON_Delete(root);
        return NULL;
    }

    cJSON_AddStringToObject(
        pet,
        "default_package_id",
        default_pet_package_id && default_pet_package_id[0]
            ? default_pet_package_id
            : "guga.codex-pet");

    cJSON *terminal = cJSON_AddObjectToObject(root, "terminal");
    if (terminal) {
        cJSON_AddStringToObject(terminal, "security_level", runtime_config_get_terminal_security_level());
    }

    dir = opendir(path_spiffs_base());
    if (dir) {
        while ((entry = readdir(dir)) != NULL) {
            const char *name = entry->d_name;
            const char *suffix = ".codex-pet";
            size_t name_len = strlen(name);
            size_t suffix_len = strlen(suffix);
            char pet_json_path[BUF_LARGE];
            char *pet_json_text = NULL;
            cJSON *pet_meta = NULL;
            cJSON *item = NULL;
            const char *pet_id = NULL;
            const char *display_name = NULL;

            if (name[0] == '.') {
                continue;
            }
            if (name_len <= suffix_len || strcmp(name + name_len - suffix_len, suffix) != 0) {
                continue;
            }

            snprintf(pet_json_path, sizeof(pet_json_path), "%s/%s/pet.json",
                     path_spiffs_base(), name);
            pet_json_text = read_text_file(pet_json_path, 64 * 1024);
            if (!pet_json_text) {
                continue;
            }

            pet_meta = cJSON_Parse(pet_json_text);
            kfree(pet_json_text);
            if (!pet_meta || !cJSON_IsObject(pet_meta)) {
                cJSON_Delete(pet_meta);
                continue;
            }

            item = cJSON_CreateObject();
            if (!item) {
                cJSON_Delete(pet_meta);
                continue;
            }

            pet_id = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(pet_meta, "id"));
            display_name = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(pet_meta, "displayName"));

            cJSON_AddStringToObject(item, "package_id", name);
            cJSON_AddStringToObject(item, "pet_id", pet_id && pet_id[0] ? pet_id : name);
            cJSON_AddStringToObject(item, "display_name",
                                    display_name && display_name[0] ? display_name : name);
            cJSON_AddItemToArray(packages, item);
            cJSON_Delete(pet_meta);
        }
        closedir(dir);
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

/* qsort 比较函数：按 latest_ts 降序排列会话记录。 */
static int compare_session_records_recent(const void *a, const void *b)
{
    const session_record_t *ra = (const session_record_t *)a;
    const session_record_t *rb = (const session_record_t *)b;
    if (ra->latest_ts < rb->latest_ts) return 1;
    if (ra->latest_ts > rb->latest_ts) return -1;
    return strcmp(ra->chat_id, rb->chat_id);
}

/* 构建 /api/sessions 响应：列出所有会话（chat_id、时间戳、是否有历史/事实/摘要）。 */
static char *build_sessions_json(void)
{
    session_record_t records[128];
    int count = 0;
    if (session_store_list_records(records, sizeof(records) / sizeof(records[0]), &count) != 0) {
        return NULL;
    }

    qsort(records, (size_t)count, sizeof(records[0]), compare_session_records_recent);

    cJSON *root = cJSON_CreateObject();
    cJSON *items = cJSON_AddArrayToObject(root, "sessions");
    if (!root || !items) {
        cJSON_Delete(root);
        return NULL;
    }

    for (int i = 0; i < count; i++) {
        cJSON *item = cJSON_CreateObject();
        if (!item) {
            continue;
        }
        cJSON_AddStringToObject(item, "chat_id", records[i].chat_id);
        cJSON_AddNumberToObject(item, "latest_ts", (double)records[i].latest_ts);
        cJSON_AddBoolToObject(item, "has_history", records[i].has_history);
        cJSON_AddBoolToObject(item, "has_facts", records[i].has_facts);
        cJSON_AddBoolToObject(item, "has_summary", records[i].has_summary);
        cJSON_AddItemToArray(items, item);
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

/* 构建 /api/session_history 响应：提取 assistant 消息的纯文本内容（去掉 JSON 包装）。 */
static cJSON *build_session_history_messages_array(const char *chat_id)
{
    if (!chat_id || !chat_id[0]) {
        return NULL;
    }

    char history_json[LLM_STREAM_BUF_SIZE];
    history_json[0] = '\0';
    if (session_store_get_history_json(chat_id, history_json, sizeof(history_json), AGENT_MAX_HISTORY) != 0) {
        return NULL;
    }

    cJSON *messages = cJSON_Parse(history_json);
    if (!messages) {
        messages = cJSON_CreateArray();
    }
    if (!messages) {
        return NULL;
    }

    cJSON *msg = NULL;
    cJSON_ArrayForEach(msg, messages) {
        cJSON *role = cJSON_GetObjectItemCaseSensitive(msg, "role");
        cJSON *content = cJSON_GetObjectItemCaseSensitive(msg, "content");
        if (!role || !cJSON_IsString(role) || !content || !cJSON_IsString(content)) {
            continue;
        }
        if (strcmp(role->valuestring, "assistant") != 0) {
            continue;
        }

        cJSON *parsed = cJSON_Parse(content->valuestring);
        if (!parsed || !cJSON_IsObject(parsed)) {
            cJSON_Delete(parsed);
            continue;
        }

        cJSON *text = cJSON_GetObjectItemCaseSensitive(parsed, "text");
        if (text && cJSON_IsString(text)) {
            cJSON_ReplaceItemInObjectCaseSensitive(msg, "content", cJSON_CreateString(text->valuestring));
            cJSON *reasoning = cJSON_GetObjectItemCaseSensitive(parsed, "reasoning");
            if (reasoning && cJSON_IsString(reasoning) && reasoning->valuestring[0]) {
                cJSON_AddStringToObject(msg, "reasoning", reasoning->valuestring);
            }
        }
        cJSON_Delete(parsed);
    }

    return messages;
}

static char *build_session_history_json(const char *chat_id)
{
    cJSON *messages = build_session_history_messages_array(chat_id);
    if (!messages) {
        return NULL;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        cJSON_Delete(messages);
        return NULL;
    }
    cJSON_AddStringToObject(root, "chat_id", chat_id);
    cJSON_AddItemToObject(root, "messages", messages);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

static char *build_session_state_json(const char *chat_id)
{
    char *subagent_json = NULL;
    cJSON *subagent_root = NULL;
    cJSON *root = NULL;
    cJSON *messages = NULL;
    cJSON *history_window = NULL;
    cJSON *history_cursor = NULL;
    char *json = NULL;
    session_history_window_meta_t history_meta;

    if (!chat_id || !chat_id[0]) {
        return NULL;
    }

    messages = build_session_history_messages_array(chat_id);
    memset(&history_meta, 0, sizeof(history_meta));
    history_meta.limit = AGENT_MAX_HISTORY;
    history_meta.next_seq = 1;
    if (session_store_get_history_window_meta(chat_id, AGENT_MAX_HISTORY, &history_meta) != 0) {
        history_meta.count = messages ? cJSON_GetArraySize(messages) : 0;
        history_meta.total = history_meta.count;
    }
    subagent_json = delegate_parent_subagent_state_json_build(chat_id);
    if (subagent_json) {
        subagent_root = cJSON_Parse(subagent_json);
    }

    root = cJSON_CreateObject();
    if (!root) {
        goto done;
    }
    cJSON_AddStringToObject(root, "chat_id", chat_id);

    if (!messages) {
        messages = cJSON_CreateArray();
    }
    if (!messages) {
        goto done;
    }
    cJSON_AddItemToObject(root, "history", messages);
    messages = NULL;

    history_window = cJSON_CreateObject();
    if (!history_window) {
        goto done;
    }
    cJSON_AddNumberToObject(history_window, "limit", history_meta.limit);
    cJSON_AddNumberToObject(history_window, "count", history_meta.count);
    cJSON_AddNumberToObject(history_window, "total", history_meta.total);
    cJSON_AddBoolToObject(history_window, "truncated", history_meta.truncated);
    cJSON_AddNumberToObject(history_window, "first_seq", history_meta.first_seq);
    cJSON_AddNumberToObject(history_window, "last_seq", history_meta.last_seq);
    cJSON_AddNumberToObject(history_window, "high_water_seq", history_meta.high_water_seq);
    cJSON_AddNumberToObject(history_window, "next_seq", history_meta.next_seq);
    cJSON_AddBoolToObject(history_window, "has_more", history_meta.has_more);
    cJSON_AddItemToObject(root, "history_window", history_window);
    history_window = NULL;

    history_cursor = cJSON_CreateObject();
    if (!history_cursor) {
        goto done;
    }
    cJSON_AddNumberToObject(history_cursor, "after_seq", 0);
    cJSON_AddNumberToObject(history_cursor, "visible_seq", history_meta.last_seq);
    cJSON_AddNumberToObject(history_cursor, "first_visible_seq", history_meta.first_seq);
    cJSON_AddNumberToObject(history_cursor, "next_seq", history_meta.next_seq);
    cJSON_AddNumberToObject(history_cursor, "high_water_seq", history_meta.high_water_seq);
    cJSON_AddBoolToObject(history_cursor, "has_more", history_meta.has_more);
    cJSON_AddBoolToObject(history_cursor, "replay_reset", history_meta.truncated);
    cJSON_AddItemToObject(root, "history_cursor", history_cursor);
    history_cursor = NULL;

    if (subagent_root) {
        cJSON_AddItemToObject(root, "subagent", subagent_root);
        subagent_root = NULL;
    } else {
        cJSON *empty = cJSON_CreateObject();
        if (!empty) {
            goto done;
        }
        cJSON_AddStringToObject(empty, "chat_id", chat_id);
        cJSON_AddItemToObject(empty, "coordinators", cJSON_CreateArray());
        cJSON_AddItemToObject(root, "subagent", empty);
    }

    cJSON *ui = cJSON_CreateObject();
    if (!ui) {
        goto done;
    }
    cJSON_AddItemToObject(root, "ui", ui);

    json = cJSON_PrintUnformatted(root);

done:
    kfree(subagent_json);
    cJSON_Delete(subagent_root);
    cJSON_Delete(messages);
    cJSON_Delete(history_window);
    cJSON_Delete(history_cursor);
    cJSON_Delete(root);
    return json;
}

static char *build_session_events_json(const char *chat_id)
{
    char *subagent_json = NULL;
    cJSON *subagent_root = NULL;
    cJSON *root = NULL;
    cJSON *messages = NULL;
    cJSON *events = NULL;
    cJSON *cursor = NULL;
    char *json = NULL;
    session_history_window_meta_t history_meta;
    int event_seq = 0;

    if (!chat_id || !chat_id[0]) {
        return NULL;
    }

    messages = build_session_history_messages_array(chat_id);
    memset(&history_meta, 0, sizeof(history_meta));
    history_meta.limit = AGENT_MAX_HISTORY;
    history_meta.next_seq = 1;
    if (session_store_get_history_window_meta(chat_id, AGENT_MAX_HISTORY, &history_meta) != 0) {
        history_meta.count = messages ? cJSON_GetArraySize(messages) : 0;
        history_meta.total = history_meta.count;
    }

    subagent_json = delegate_parent_subagent_state_json_build(chat_id);
    if (subagent_json) {
        subagent_root = cJSON_Parse(subagent_json);
    }

    root = cJSON_CreateObject();
    events = cJSON_CreateArray();
    cursor = cJSON_CreateObject();
    if (!root || !events || !cursor) {
        goto done;
    }

    cJSON_AddStringToObject(root, "chat_id", chat_id);
    cJSON_AddStringToObject(root, "stream_kind", "session_events");
    cJSON_AddNumberToObject(cursor, "after_seq", 0);
    cJSON_AddNumberToObject(cursor, "visible_seq", history_meta.last_seq);
    cJSON_AddNumberToObject(cursor, "first_visible_seq", history_meta.first_seq);
    cJSON_AddNumberToObject(cursor, "next_seq", history_meta.next_seq);
    cJSON_AddNumberToObject(cursor, "high_water_seq", history_meta.high_water_seq);
    cJSON_AddBoolToObject(cursor, "has_more", history_meta.has_more);
    cJSON_AddBoolToObject(cursor, "replay_reset", history_meta.truncated);
    cJSON_AddItemToObject(root, "cursor", cursor);
    cursor = NULL;

    if (messages && cJSON_IsArray(messages)) {
        int size = cJSON_GetArraySize(messages);
        for (int idx = 0; idx < size; idx++) {
            cJSON *msg = cJSON_GetArrayItem(messages, idx);
            const cJSON *seq = msg ? cJSON_GetObjectItemCaseSensitive(msg, "seq") : NULL;
            cJSON *event = cJSON_CreateObject();
            cJSON *payload = msg ? cJSON_Duplicate(msg, 1) : NULL;
            if (!event || !payload) {
                cJSON_Delete(event);
                cJSON_Delete(payload);
                continue;
            }
            cJSON_AddStringToObject(event, "id", "history");
            cJSON_AddStringToObject(event, "type", "history_message");
            cJSON_AddNumberToObject(event,
                                    "seq",
                                    cJSON_IsNumber(seq) ? seq->valuedouble : (double)(idx + 1));
            cJSON_AddItemToObject(event, "payload", payload);
            cJSON_AddItemToArray(events, event);
            event_seq = idx + 1;
        }
    }

    if (subagent_root) {
        cJSON *coordinators = cJSON_GetObjectItemCaseSensitive(subagent_root, "coordinators");
        if (coordinators && cJSON_IsArray(coordinators)) {
            cJSON *coordinator = NULL;
            cJSON_ArrayForEach(coordinator, coordinators) {
                cJSON *coordinator_event = NULL;
                cJSON *coordinator_payload = NULL;
                cJSON *agents = NULL;
                cJSON *agent = NULL;
                const char *coordinator_id = NULL;

                if (!coordinator || !cJSON_IsObject(coordinator)) {
                    continue;
                }

                coordinator_payload = cJSON_Duplicate(coordinator, 1);
                coordinator_event = cJSON_CreateObject();
                if (coordinator_event && coordinator_payload) {
                    cJSON_AddStringToObject(coordinator_event, "id", "coordinator_snapshot");
                    cJSON_AddStringToObject(coordinator_event, "type", "coordinator_snapshot");
                    cJSON_AddNumberToObject(coordinator_event, "seq", (double)(++event_seq));
                    cJSON_AddItemToObject(coordinator_event, "payload", coordinator_payload);
                    cJSON_AddItemToArray(events, coordinator_event);
                } else {
                    cJSON_Delete(coordinator_event);
                    cJSON_Delete(coordinator_payload);
                }

                coordinator_id = cJSON_GetStringValue(
                    cJSON_GetObjectItemCaseSensitive(coordinator, "coordinator_id"));
                agents = cJSON_GetObjectItemCaseSensitive(coordinator, "agents");
                if (!agents || !cJSON_IsArray(agents)) {
                    continue;
                }

                cJSON_ArrayForEach(agent, agents) {
                    cJSON *session_event = NULL;
                    cJSON *session_payload = NULL;
                    cJSON *child_session = NULL;

                    if (!agent || !cJSON_IsObject(agent)) {
                        continue;
                    }

                    child_session = cJSON_GetObjectItemCaseSensitive(agent, "child_session");
                    if (!child_session || !cJSON_IsObject(child_session)) {
                        continue;
                    }

                    session_payload = cJSON_CreateObject();
                    session_event = cJSON_CreateObject();
                    if (!session_payload || !session_event) {
                        cJSON_Delete(session_payload);
                        cJSON_Delete(session_event);
                        continue;
                    }

                    cJSON_AddStringToObject(session_payload, "coordinator_id",
                                            coordinator_id ? coordinator_id : "");
                    cJSON_AddStringToObject(session_payload, "task_id",
                                            cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(agent, "task_id")) ?: "");
                    cJSON_AddStringToObject(session_payload, "task_key",
                                            cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(agent, "task_key")) ?: "");
                    cJSON_AddStringToObject(session_payload, "session_id",
                                            cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(agent, "session_id")) ?: "");
                    cJSON_AddStringToObject(session_payload, "subagent_type",
                                            cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(agent, "subagent_type")) ?: "");
                    cJSON_AddStringToObject(session_payload, "status",
                                            cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(agent, "status")) ?: "");
                    cJSON_AddStringToObject(session_payload, "task",
                                            cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(agent, "description")) ?: "");
                    cJSON_AddItemToObject(session_payload, "agent", cJSON_Duplicate(agent, 1));

                    cJSON_AddStringToObject(session_event, "id", "subagent_session");
                    cJSON_AddStringToObject(session_event, "type", "subagent_session");
                    cJSON_AddNumberToObject(session_event, "seq", (double)(++event_seq));
                    cJSON_AddItemToObject(session_event, "payload", session_payload);
                    cJSON_AddItemToArray(events, session_event);
                }
            }
        }

        {
            cJSON *event = cJSON_CreateObject();
            cJSON *payload = cJSON_Duplicate(subagent_root, 1);
            if (event && payload) {
                cJSON_AddStringToObject(event, "id", "subagent_snapshot");
                cJSON_AddStringToObject(event, "type", "subagent_snapshot");
                cJSON_AddNumberToObject(event, "seq", (double)(++event_seq));
                cJSON_AddItemToObject(event, "payload", payload);
                cJSON_AddItemToArray(events, event);
            } else {
                cJSON_Delete(event);
                cJSON_Delete(payload);
            }
        }
    }

    cJSON_AddItemToObject(root, "events", events);
    events = NULL;
    if (messages) {
        cJSON_AddItemToObject(root, "history", messages);
        messages = NULL;
    } else {
        cJSON_AddItemToObject(root, "history", cJSON_CreateArray());
    }
    if (subagent_root) {
        cJSON_AddItemToObject(root, "subagent", subagent_root);
        subagent_root = NULL;
    } else {
        cJSON *empty = cJSON_CreateObject();
        if (!empty) {
            goto done;
        }
        cJSON_AddStringToObject(empty, "chat_id", chat_id);
        cJSON_AddItemToObject(empty, "coordinators", cJSON_CreateArray());
        cJSON_AddItemToObject(root, "subagent", empty);
    }

    json = cJSON_PrintUnformatted(root);

done:
    kfree(subagent_json);
    cJSON_Delete(subagent_root);
    cJSON_Delete(messages);
    cJSON_Delete(events);
    cJSON_Delete(cursor);
    cJSON_Delete(root);
    return json;
}

/**
 * HTTP 请求路由器。
 * 在 ws_server_host.c 的主循环中被调用，当客户端请求不包含 WebSocket 升级头时，
 * 本函数路径路由处理普通 HTTP 请求。
 *
 * 路由表：
 *   GET  /                    → index.html（静态文件或内置降级 HTML）
 *   GET  /index.html          → 同 /
 *   GET  /*.css / *.js        → Web 根目录前端资源
 *   GET  /pet.js              → 宠物脚本
 *   GET  /pets/...            → 宠物资源（图片/配置，8MB 上限二进制）
 *   GET  /health              → "ok"
 *   GET  /api/context_stats   → {model, used_tokens, context_limit_tokens, usage_percent}
 *   GET  /api/sessions        → [{chat_id, latest_ts, has_history, has_facts, has_summary}]
 *   GET  /api/session_history → {chat_id, messages: [{role, content, reasoning}]}
 *   GET  /api/session_state   → {chat_id, history: [...], subagent: {...}, ui: {...}}
 *   GET  /api/session_events  → {chat_id, events: [...], history: [...], subagent: {...}, cursor: {...}}
 *   GET  /api/ui_config       → {pet: {packages, default_package_id}, terminal: {security_level}}
 *   GET  /api/subagent_state  → {chat_id, coordinators: [...]}
 *   POST /api/session_delete  → 删除会话
 *   POST /api/terminal_security → 设置终端安全级别
 *   其他 → 404
 *
 * @param client_fd         客户端 socket fd
 * @param req               HTTP 请求原始文本
 * @param ui_fallback_html  当 index.html 不存在时使用的降级 HTML
 * @return 0 表示正常处理（fd 已关闭），-1 表示错误
 */
int ws_http_handle_request(int client_fd, const char *req, const char *ui_fallback_html)
{
    char method[8] = {0};
    char path[256] = {0};
    parse_request_line(req, method, sizeof(method), path, sizeof(path));
    char *query = NULL;
    split_path_and_query(path, &query);

    /* ── POST 路由 ── */

    /* POST /api/session_delete?chat_id=... → 删除指定会话 */
    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/session_delete") == 0) {
        char chat_id[64] = {0};
        query_get_value(query, "chat_id", chat_id, sizeof(chat_id));
        if (!is_safe_chat_id(chat_id)) {
            http_send_response(client_fd, "400 Bad Request",
                               "application/json; charset=utf-8",
                               "{\"error\":\"invalid_chat_id\"}");
            return 0;
        }

        err_t err = session_store_clear(chat_id);
        if (err != 0 && err != ERR_NOT_FOUND) {
            http_send_response(client_fd, "500 Internal Server Error",
                               "application/json; charset=utf-8",
                               "{\"error\":\"delete_failed\"}");
            return 0;
        }
        http_send_response(client_fd, "200 OK",
                           "application/json; charset=utf-8",
                           "{\"ok\":true}");
        return 0;
    }

    /* POST /api/terminal_security?level=plan|build → 设置终端安全级别 */
    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/terminal_security") == 0) {
        char level[32] = {0};
        const char *body = http_request_body(req);
        query_get_value(query, "level", level, sizeof(level));
        if (!level[0]) {
            json_body_get_string_field(body, "level", level, sizeof(level));
        }
        if (!level[0]) {
            json_body_get_string_field(body, "security_level", level, sizeof(level));
        }
        if (!is_valid_terminal_security_level(level)) {
            http_send_response(client_fd, "400 Bad Request",
                               "application/json; charset=utf-8",
                               "{\"error\":\"invalid_level\"}");
            return 0;
        }

        err_t err = runtime_config_set_terminal_security_level(level);
        if (err != 0) {
            http_send_response(client_fd, "500 Internal Server Error",
                               "application/json; charset=utf-8",
                               "{\"error\":\"save_failed\"}");
            return 0;
        }
        http_send_response(client_fd, "200 OK",
                           "application/json; charset=utf-8",
                           "{\"ok\":true}");
        return 0;
    }

    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/subagent_state_deltas") == 0) {
        char chat_id[64] = {0};
        const char *body = http_request_body(req);
        cJSON *parsed = NULL;
        cJSON *chat_id_item = NULL;
        char *json = NULL;

        if (!body || !body[0]) {
            http_send_response(client_fd, "400 Bad Request",
                               "application/json; charset=utf-8",
                               "{\"error\":\"missing_request_body\"}");
            return 0;
        }

        parsed = cJSON_Parse(body);
        if (!parsed) {
            http_send_response(client_fd, "400 Bad Request",
                               "application/json; charset=utf-8",
                               "{\"error\":\"invalid_json_body\"}");
            return 0;
        }

        chat_id_item = cJSON_GetObjectItemCaseSensitive(parsed, "chat_id");
        if (cJSON_IsString(chat_id_item) && chat_id_item->valuestring) {
            snprintf(chat_id, sizeof(chat_id), "%s", chat_id_item->valuestring);
        }
        cJSON_Delete(parsed);

        if (!chat_id[0]) {
            http_send_response(client_fd, "400 Bad Request",
                               "application/json; charset=utf-8",
                               "{\"error\":\"missing_chat_id\"}");
            return 0;
        }

        json = delegate_subagent_session_deltas_json_build(chat_id, body);
        if (!json) {
            http_send_response(client_fd, "404 Not Found",
                               "application/json; charset=utf-8",
                               "{\"error\":\"subagent_state_deltas_unavailable\"}");
            return 0;
        }
        http_send_response(client_fd, "200 OK",
                           "application/json; charset=utf-8", json);
        kfree(json);
        return 0;
    }

    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/subagent_state_delta_chat") == 0) {
        char chat_id[64] = {0};
        unsigned long after_visible_revision = 0;
        const char *body = http_request_body(req);
        cJSON *parsed = NULL;
        cJSON *chat_id_item = NULL;
        cJSON *after_item = NULL;
        char *json = NULL;

        if (!body || !body[0]) {
            http_send_response(client_fd, "400 Bad Request",
                               "application/json; charset=utf-8",
                               "{\"error\":\"missing_request_body\"}");
            return 0;
        }

        parsed = cJSON_Parse(body);
        if (!parsed) {
            http_send_response(client_fd, "400 Bad Request",
                               "application/json; charset=utf-8",
                               "{\"error\":\"invalid_json_body\"}");
            return 0;
        }

        chat_id_item = cJSON_GetObjectItemCaseSensitive(parsed, "chat_id");
        if (cJSON_IsString(chat_id_item) && chat_id_item->valuestring) {
            snprintf(chat_id, sizeof(chat_id), "%s", chat_id_item->valuestring);
        }
        after_item = cJSON_GetObjectItemCaseSensitive(parsed, "after_visible_revision");
        if (cJSON_IsNumber(after_item) && after_item->valuedouble > 0) {
            after_visible_revision = (unsigned long)after_item->valuedouble;
        }
        cJSON_Delete(parsed);

        if (!chat_id[0]) {
            http_send_response(client_fd, "400 Bad Request",
                               "application/json; charset=utf-8",
                               "{\"error\":\"missing_chat_id\"}");
            return 0;
        }

        json = delegate_parent_subagent_state_delta_json_build(chat_id, after_visible_revision, body);
        if (!json) {
            http_send_response(client_fd, "404 Not Found",
                               "application/json; charset=utf-8",
                               "{\"error\":\"subagent_state_delta_chat_unavailable\"}");
            return 0;
        }
        http_send_response(client_fd, "200 OK",
                           "application/json; charset=utf-8", json);
        kfree(json);
        return 0;
    }

    /* ── 仅接受 GET/POST，其他方法返回 405 ── */
    if (strcmp(method, "GET") != 0) {
        http_send_response(client_fd, "405 Method Not Allowed",
                           "text/plain; charset=utf-8", "Method Not Allowed");
        return 0;
    }

    /* ── GET 路由 ── */

    /* GET /, /index.html → 主页，文件不存在时使用内置降级 HTML */
    if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
        char asset_path[BUF_LARGE];
        snprintf(asset_path, sizeof(asset_path), "%s/index.html", path_web_dir());
        http_send_static_file_or_fallback(
            client_fd,
            "text/html; charset=utf-8",
            asset_path,
            ui_fallback_html);
        return 0;
    }

    /* GET /*.css / *.js → Web 根目录前端静态资源 */
    {
        const char *asset_name = NULL;
        const char *asset_type = NULL;
        if (path[0] == '/') {
            asset_name = path + 1;
        }
        if (asset_name && is_safe_web_asset_name(asset_name)) {
            const char *ext = strrchr(asset_name, '.');
            if (ext && strcmp(ext, ".css") == 0) {
                asset_type = "text/css; charset=utf-8";
            } else if (ext && strcmp(ext, ".js") == 0) {
                asset_type = "application/javascript; charset=utf-8";
            }
            if (asset_type) {
                char asset_path[BUF_LARGE];
                snprintf(asset_path, sizeof(asset_path), "%s/%s", path_web_dir(), asset_name);
                http_send_static_file_or_fallback(
                    client_fd,
                    asset_type,
                    asset_path,
                    "");
                return 0;
            }
        }
    }

    /* GET /pets/... → 宠物资源（二进制文件，8MB 上限，安全路径校验） */
    if (strncmp(path, "/pets/", 6) == 0) {
        const char *relative = path + 6;
        if (!is_safe_asset_relative_path(relative)) {
            http_send_response(client_fd, "400 Bad Request",
                               "text/plain; charset=utf-8", "Bad Request");
            return 0;
        }

        char asset_path[BUF_LARGE];
        snprintf(asset_path, sizeof(asset_path), "%s/%s",
                 path_spiffs_base(), relative);
        http_send_binary_file(client_fd, asset_path);
        return 0;
    }

    /* GET /health → 健康检查 */
    if (strcmp(path, "/health") == 0) {
        http_send_response(client_fd, "200 OK",
                           "text/plain; charset=utf-8", "ok");
        return 0;
    }

    /* GET /api/context_stats → 上下文使用统计（tokens/模型） */
    if (strcmp(path, "/api/context_stats") == 0) {
        char chat_id[64] = {0};
        query_get_value(query, "chat_id", chat_id, sizeof(chat_id));
        if (!chat_id[0]) {
            snprintf(chat_id, sizeof(chat_id), "web_unknown");
        }
        char *json = build_context_stats_json(chat_id);
        if (!json) {
            http_send_response(client_fd, "500 Internal Server Error",
                               "application/json; charset=utf-8",
                               "{\"error\":\"stats_unavailable\"}");
            return 0;
        }
        http_send_response(client_fd, "200 OK",
                           "application/json; charset=utf-8", json);
        kfree(json);
        return 0;
    }

    /* GET /api/sessions → 会话列表（按最近活跃排序） */
    if (strcmp(path, "/api/sessions") == 0) {
        char *json = build_sessions_json();
        if (!json) {
            http_send_response(client_fd, "500 Internal Server Error",
                               "application/json; charset=utf-8",
                               "{\"error\":\"sessions_unavailable\"}");
            return 0;
        }
        http_send_response(client_fd, "200 OK",
                           "application/json; charset=utf-8", json);
        kfree(json);
        return 0;
    }

    /* GET /api/session_history?chat_id=... → 会话历史（提取 assistant 纯文本） */
    if (strcmp(path, "/api/session_history") == 0) {
        char chat_id[64] = {0};
        query_get_value(query, "chat_id", chat_id, sizeof(chat_id));
        if (!chat_id[0]) {
            http_send_response(client_fd, "400 Bad Request",
                               "application/json; charset=utf-8",
                               "{\"error\":\"missing_chat_id\"}");
            return 0;
        }

        char *json = build_session_history_json(chat_id);
        if (!json) {
            http_send_response(client_fd, "500 Internal Server Error",
                               "application/json; charset=utf-8",
                               "{\"error\":\"history_unavailable\"}");
            return 0;
        }
        http_send_response(client_fd, "200 OK",
                           "application/json; charset=utf-8", json);
        kfree(json);
        return 0;
    }

    /* GET /api/session_state?chat_id=... → 统一 session-first 恢复投影 */
    if (strcmp(path, "/api/session_state") == 0) {
        char chat_id[64] = {0};
        query_get_value(query, "chat_id", chat_id, sizeof(chat_id));
        if (!chat_id[0]) {
            http_send_response(client_fd, "400 Bad Request",
                               "application/json; charset=utf-8",
                               "{\"error\":\"missing_chat_id\"}");
            return 0;
        }

        char *json = build_session_state_json(chat_id);
        if (!json) {
            http_send_response(client_fd, "500 Internal Server Error",
                               "application/json; charset=utf-8",
                               "{\"error\":\"session_state_unavailable\"}");
            return 0;
        }
        http_send_response(client_fd, "200 OK",
                           "application/json; charset=utf-8", json);
        kfree(json);
        return 0;
    }

    /* GET /api/session_events?chat_id=... → 统一 session-first event feed 骨架 */
    if (strcmp(path, "/api/session_events") == 0) {
        char chat_id[64] = {0};
        query_get_value(query, "chat_id", chat_id, sizeof(chat_id));
        if (!chat_id[0]) {
            http_send_response(client_fd, "400 Bad Request",
                               "application/json; charset=utf-8",
                               "{\"error\":\"missing_chat_id\"}");
            return 0;
        }

        char *json = build_session_events_json(chat_id);
        if (!json) {
            http_send_response(client_fd, "500 Internal Server Error",
                               "application/json; charset=utf-8",
                               "{\"error\":\"session_events_unavailable\"}");
            return 0;
        }
        http_send_response(client_fd, "200 OK",
                           "application/json; charset=utf-8", json);
        kfree(json);
        return 0;
    }

    /* GET /api/ui_config → UI 配置（宠物包、终端安全级别） */
    if (strcmp(path, "/api/ui_config") == 0) {
        char *json = build_ui_config_json();
        if (!json) {
            http_send_response(client_fd, "500 Internal Server Error",
                               "application/json; charset=utf-8",
                               "{\"error\":\"ui_config_unavailable\"}");
            return 0;
        }
        http_send_response(client_fd, "200 OK",
                           "application/json; charset=utf-8", json);
        kfree(json);
        return 0;
    }

    if (strcmp(path, "/api/subagent_state") == 0) {
        char chat_id[64] = {0};
        query_get_value(query, "chat_id", chat_id, sizeof(chat_id));
        if (!chat_id[0]) {
            http_send_response(client_fd, "400 Bad Request",
                               "application/json; charset=utf-8",
                               "{\"error\":\"missing_chat_id\"}");
            return 0;
        }

        char *json = delegate_parent_subagent_state_json_build(chat_id);
        if (!json) {
            http_send_response(client_fd, "404 Not Found",
                               "application/json; charset=utf-8",
                               "{\"error\":\"subagent_state_unavailable\"}");
            return 0;
        }
        http_send_response(client_fd, "200 OK",
                           "application/json; charset=utf-8", json);
        kfree(json);
        return 0;
    }

    if (strcmp(path, "/api/subagent_state_delta") == 0) {
        char task_id[64] = {0};
        char history_after_seq_raw[32] = {0};
        char frame_after_seq_raw[32] = {0};
        char commit_after_seq_raw[32] = {0};
        unsigned long history_after_seq = 0;
        unsigned long frame_after_seq = 0;
        unsigned long commit_after_seq = 0;

        query_get_value(query, "task_id", task_id, sizeof(task_id));
        query_get_value(query, "history_after_seq", history_after_seq_raw, sizeof(history_after_seq_raw));
        query_get_value(query, "frame_after_seq", frame_after_seq_raw, sizeof(frame_after_seq_raw));
        query_get_value(query, "commit_after_seq", commit_after_seq_raw, sizeof(commit_after_seq_raw));
        if (!task_id[0]) {
            http_send_response(client_fd, "400 Bad Request",
                               "application/json; charset=utf-8",
                               "{\"error\":\"missing_task_id\"}");
            return 0;
        }

        if (history_after_seq_raw[0]) {
            history_after_seq = strtoul(history_after_seq_raw, NULL, 10);
        }
        if (frame_after_seq_raw[0]) {
            frame_after_seq = strtoul(frame_after_seq_raw, NULL, 10);
        }
        if (commit_after_seq_raw[0]) {
            commit_after_seq = strtoul(commit_after_seq_raw, NULL, 10);
        }

        char *json = delegate_subagent_session_delta_json_build(task_id,
                                                                history_after_seq,
                                                                frame_after_seq,
                                                                commit_after_seq);
        if (!json) {
            http_send_response(client_fd, "404 Not Found",
                               "application/json; charset=utf-8",
                               "{\"error\":\"subagent_state_delta_unavailable\"}");
            return 0;
        }
        http_send_response(client_fd, "200 OK",
                           "application/json; charset=utf-8", json);
        kfree(json);
        return 0;
    }

    if (strcmp(path, "/favicon.ico") == 0) {
        http_send_response(client_fd, "204 No Content",
                           "image/x-icon", NULL);
        return 0;
    }

    http_send_response(client_fd, "404 Not Found",
                       "text/plain; charset=utf-8", "Not Found");
    return 0;
}
