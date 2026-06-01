#include "gateway/ws_http_helpers.h"

#include "app/daima_paths.h"
#include "app/runtime_config.h"
#include "agent/context_builder.h"
#include "llm/llm_proxy.h"
#include "memory/session_store.h"
#include "daima_config.h"
#include "daima_log.h"
#include "cJSON.h"

#include <dirent.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>

static const char *TAG = "ws";

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
    char *buf = calloc(1, (size_t)size + 1);
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
        buf = calloc(1, 1);
    } else {
        buf = malloc((size_t)size);
    }
    if (!buf) {
        fclose(f);
        return NULL;
    }

    size_t n = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if (n != (size_t)size) {
        free(buf);
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
        free(body);
        return;
    }

    DAIMA_LOGW(TAG, "Static asset missing, fallback to built-in body: %s", path ? path : "(null)");
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
    free(body);
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

    dir = opendir(daima_path_spiffs_base());
    if (dir) {
        while ((entry = readdir(dir)) != NULL) {
            const char *name = entry->d_name;
            const char *suffix = ".codex-pet";
            size_t name_len = strlen(name);
            size_t suffix_len = strlen(suffix);
            char pet_json_path[1024];
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
            free(pet_json_text);
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

int ws_http_handle_request(int client_fd, const char *req, const char *ui_fallback_html)
{
    char method[8] = {0};
    char path[256] = {0};
    parse_request_line(req, method, sizeof(method), path, sizeof(path));
    char *query = NULL;
    split_path_and_query(path, &query);

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
        char asset_path[1024];
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

        char asset_path[1024];
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
        free(json);
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
        free(json);
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
