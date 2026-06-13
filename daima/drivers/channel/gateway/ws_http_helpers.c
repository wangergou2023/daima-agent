#include "drivers/channel/gateway/ws_http_helpers.h"

#include "paths.h"
#include "runtime.h"
#include "context_build.h"
#include "drivers/llm/llm_proxy.h"
#include "drivers/memory/session_store.h"
#include "autoconf.h"
#include "linux/printk.h"
#include "cJSON.h"

#include <dirent.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <time.h>
#include "linux/slab.h"
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

static void http_send_response(int fd, const char *status,
                               const char *content_type, const char *body)
{
    size_t body_len = body ? strlen(body) : 0;
    http_send_response_bytes(fd, status,
                             content_type ? content_type : "text/plain; charset=utf-8",
                             body, body_len);
}

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

static bool is_valid_terminal_security_level(const char *level)
{
    return level &&
           (strcmp(level, "plan") == 0 ||
            strcmp(level, "build") == 0);
}

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

static char *build_context_stats_json(const char *chat_id)
{
    char history_json[DAIMA_LLM_STREAM_BUF_SIZE];
    char system_prompt[DAIMA_CONTEXT_BUF_SIZE];
    history_json[0] = '\0';
    system_prompt[0] = '\0';

    session_store_get_history_json(
        chat_id && chat_id[0] ? chat_id : "web_unknown",
        history_json,
        sizeof(history_json),
        DAIMA_AGENT_MAX_HISTORY);
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

    dir = opendir(daima_path_spiffs_base());
    if (dir) {
        while ((entry = readdir(dir)) != NULL) {
            const char *name = entry->d_name;
            const char *suffix = ".codex-pet";
            size_t name_len = strlen(name);
            size_t suffix_len = strlen(suffix);
            char pet_json_path[DAIMA_BUF_LARGE];
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
                     daima_path_spiffs_base(), name);
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

static int compare_session_records_recent(const void *a, const void *b)
{
    const daima_session_record_t *ra = (const daima_session_record_t *)a;
    const daima_session_record_t *rb = (const daima_session_record_t *)b;
    if (ra->latest_ts < rb->latest_ts) return 1;
    if (ra->latest_ts > rb->latest_ts) return -1;
    return strcmp(ra->chat_id, rb->chat_id);
}

static char *build_sessions_json(void)
{
    daima_session_record_t records[128];
    int count = 0;
    if (session_store_list_records(records, sizeof(records) / sizeof(records[0]), &count) != DAIMA_OK) {
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

static char *build_session_history_json(const char *chat_id)
{
    if (!chat_id || !chat_id[0]) {
        return NULL;
    }

    char history_json[DAIMA_LLM_STREAM_BUF_SIZE];
    history_json[0] = '\0';
    if (session_store_get_history_json(chat_id, history_json, sizeof(history_json), DAIMA_AGENT_MAX_HISTORY) != DAIMA_OK) {
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

int ws_http_handle_request(int client_fd, const char *req, const char *ui_fallback_html)
{
    char method[8] = {0};
    char path[256] = {0};
    parse_request_line(req, method, sizeof(method), path, sizeof(path));
    char *query = NULL;
    split_path_and_query(path, &query);

    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/session_delete") == 0) {
        char chat_id[64] = {0};
        query_get_value(query, "chat_id", chat_id, sizeof(chat_id));
        if (!is_safe_chat_id(chat_id)) {
            http_send_response(client_fd, "400 Bad Request",
                               "application/json; charset=utf-8",
                               "{\"error\":\"invalid_chat_id\"}");
            return 0;
        }

        daima_err_t err = session_store_clear(chat_id);
        if (err != DAIMA_OK && err != DAIMA_ERR_NOT_FOUND) {
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

    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/terminal_security") == 0) {
        char level[32] = {0};
        query_get_value(query, "level", level, sizeof(level));
        if (!is_valid_terminal_security_level(level)) {
            http_send_response(client_fd, "400 Bad Request",
                               "application/json; charset=utf-8",
                               "{\"error\":\"invalid_level\"}");
            return 0;
        }

        daima_err_t err = runtime_config_set_terminal_security_level(level);
        if (err != DAIMA_OK) {
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

    if (strcmp(method, "GET") != 0) {
        http_send_response(client_fd, "405 Method Not Allowed",
                           "text/plain; charset=utf-8", "Method Not Allowed");
        return 0;
    }

    if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
        http_send_static_file_or_fallback(
            client_fd,
            "text/html; charset=utf-8",
            daima_path_web_index_file(),
            ui_fallback_html);
        return 0;
    }

    if (strcmp(path, "/app.css") == 0) {
        http_send_static_file_or_fallback(
            client_fd,
            "text/css; charset=utf-8",
            daima_path_web_css_file(),
            "");
        return 0;
    }

    if (strcmp(path, "/app.js") == 0) {
        http_send_static_file_or_fallback(
            client_fd,
            "application/javascript; charset=utf-8",
            daima_path_web_js_file(),
            "");
        return 0;
    }

    if (strcmp(path, "/pet.js") == 0) {
        char asset_path[DAIMA_BUF_LARGE];
        snprintf(asset_path, sizeof(asset_path), "%s/web/pet.js", daima_path_spiffs_base());
        http_send_static_file_or_fallback(
            client_fd,
            "application/javascript; charset=utf-8",
            asset_path,
            "");
        return 0;
    }

    if (strncmp(path, "/pets/", 6) == 0) {
        const char *relative = path + 6;
        if (!is_safe_asset_relative_path(relative)) {
            http_send_response(client_fd, "400 Bad Request",
                               "text/plain; charset=utf-8", "Bad Request");
            return 0;
        }

        char asset_path[DAIMA_BUF_LARGE];
        snprintf(asset_path, sizeof(asset_path), "%s/%s",
                 daima_path_spiffs_base(), relative);
        http_send_binary_file(client_fd, asset_path);
        return 0;
    }

    if (strcmp(path, "/health") == 0) {
        http_send_response(client_fd, "200 OK",
                           "text/plain; charset=utf-8", "ok");
        return 0;
    }

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

    if (strcmp(path, "/favicon.ico") == 0) {
        http_send_response(client_fd, "204 No Content",
                           "image/x-icon", NULL);
        return 0;
    }

    http_send_response(client_fd, "404 Not Found",
                       "text/plain; charset=utf-8", "Not Found");
    return 0;
}
