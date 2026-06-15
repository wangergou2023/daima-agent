/* 飞书 OpenAPI token、发信与 WS 配置请求。 */

#include "drivers/channel/feishu/feishu_api.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cjson.h"
#include "autoconf.h"
#include "linux/printk.h"
#include "http.h"
#include "drivers/channel/feishu/feishu_http.h"
#include "linux/slab.h"
#include "linux/kernel.h"
#define FEISHU_API_BASE      "https://open.feishu.cn/open-apis"
#define FEISHU_AUTH_URL      FEISHU_API_BASE "/auth/v3/tenant_access_token/internal"
#define FEISHU_SEND_MSG_URL  FEISHU_API_BASE "/im/v1/messages"
#define FEISHU_REPLY_MSG_URL FEISHU_API_BASE "/im/v1/messages/%s/reply"
#define FEISHU_WS_CONFIG_URL "https://open.feishu.cn/callback/ws/endpoint"
#define FEISHU_CARD_CHUNK_MAX_BYTES 6000
static char s_tenant_token[BUF_MEDIUM] = {0};
static time_t s_token_expire_time = 0;
static pthread_mutex_t s_token_lock = PTHREAD_MUTEX_INITIALIZER;

void feishu_api_reset_token_cache(void)
{
    pthread_mutex_lock(&s_token_lock);
    s_tenant_token[0] = '\0';
    s_token_expire_time = 0;
    pthread_mutex_unlock(&s_token_lock);
}



static bool parse_query_param(const char *url, const char *key, char *out, size_t out_size)
{
    const char *q = strchr(url, '?');
    if (!q) return false;
    q++;
    size_t key_len = strlen(key);
    while (*q) {
        const char *eq = strchr(q, '=');
        if (!eq) break;
        const char *amp = strchr(eq + 1, '&');
        size_t name_len = (size_t)(eq - q);
        if (name_len == key_len && strncmp(q, key, key_len) == 0) {
            size_t val_len = amp ? (size_t)(amp - (eq + 1)) : strlen(eq + 1);
            size_t n = (val_len < out_size - 1) ? val_len : out_size - 1;
            memcpy(out, eq + 1, n);
            out[n] = '\0';
            return true;
        }
        if (!amp) break;
        q = amp + 1;
    }
    return false;
}

static err_t feishu_get_tenant_token_locked(const char *app_id, const char *app_secret)
{
    time_t now = time(NULL);
    if (s_tenant_token[0] != '\0' && s_token_expire_time > now + 300) {
        return 0;
    }

    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "app_id", app_id);
    cJSON_AddStringToObject(body, "app_secret", app_secret);
    char *json_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!json_str) {
        return ERR_NO_MEM;
    }

    feishu_http_response_t resp = {0};
    err_t err = feishu_http_post_json(FEISHU_AUTH_URL, NULL, json_str, TIMEOUT_SHORT, &resp);
    kfree(json_str);

    if (err != 0) {
        feishu_http_response_free(&resp);
        return err;
    }

    if (resp.status != 200) {
        pr_err("Token request failed: http=%ld", resp.status);
        feishu_http_response_free(&resp);
        return ERR_FAIL;
    }

    cJSON *root = feishu_http_parse_json(&resp);
    feishu_http_response_free(&resp);
    if (!root) {
        pr_err("Failed to parse token response");
        return ERR_FAIL;
    }

    cJSON *token = cJSON_GetObjectItem(root, "tenant_access_token");
    cJSON *expire = cJSON_GetObjectItem(root, "expire");
    if (token && cJSON_IsString(token)) {
        strscpy(s_tenant_token, token->valuestring, sizeof(s_tenant_token));
        int ttl = (expire && cJSON_IsNumber(expire)) ? expire->valueint : 7200;
        s_token_expire_time = now + ttl - 300;
        pr_info("Got tenant access token (expires in %ds)", ttl);
    } else {
        cJSON_Delete(root);
        return ERR_FAIL;
    }

    cJSON_Delete(root);
    return 0;
}

err_t feishu_api_get_tenant_token(const char *app_id,
                                       const char *app_secret,
                                       char *token,
                                       size_t token_size)
{
    if (!app_id || !app_id[0] || !app_secret || !app_secret[0] ||
        !token || token_size == 0) {
        return ERR_INVALID_ARG;
    }

    pthread_mutex_lock(&s_token_lock);
    err_t err = feishu_get_tenant_token_locked(app_id, app_secret);
    if (err == 0) {
        strscpy(token, s_tenant_token, token_size);
    }
    pthread_mutex_unlock(&s_token_lock);
    return err;
}

err_t feishu_api_pull_ws_config(const char *app_id,
                                     const char *app_secret,
                                     feishu_ws_config_t *out)
{
    if (!app_id || !app_id[0] || !app_secret || !app_secret[0] || !out) {
        return ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));
    out->ping_interval_ms = 120000;
    out->reconnect_interval_ms = 30000;
    out->reconnect_nonce_ms = 30000;

    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "AppID", app_id);
    cJSON_AddStringToObject(body, "AppSecret", app_secret);
    char *json_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!json_str) return ERR_NO_MEM;

    feishu_http_response_t resp = {0};
    err_t err = feishu_http_post_json(FEISHU_WS_CONFIG_URL, NULL, json_str, TIMEOUT_MEDIUM, &resp);
    kfree(json_str);

    if (err != 0 || resp.status != 200) {
        pr_err("WS config request failed: err=%s http=%ld", err_name(err), resp.status);
        feishu_http_response_free(&resp);
        return ERR_FAIL;
    }

    cJSON *root = feishu_http_parse_json(&resp);
    feishu_http_response_free(&resp);
    if (!root) return ERR_FAIL;

    cJSON *code = cJSON_GetObjectItem(root, "code");
    cJSON *data = cJSON_GetObjectItem(root, "data");
    cJSON *url = data ? cJSON_GetObjectItem(data, "URL") : NULL;
    cJSON *ccfg = data ? cJSON_GetObjectItem(data, "ClientConfig") : NULL;
    if (!code || !cJSON_IsNumber(code) || code->valueint != 0 || !url || !cJSON_IsString(url)) {
        pr_err("Invalid WS config response");
        cJSON_Delete(root);
        return ERR_FAIL;
    }

    strscpy(out->url, url->valuestring, sizeof(out->url));
    char sid[24] = {0};
    if (parse_query_param(out->url, "service_id", sid, sizeof(sid))) {
        out->service_id = atoi(sid);
    }
    if (ccfg) {
        cJSON *pi = cJSON_GetObjectItem(ccfg, "PingInterval");
        cJSON *ri = cJSON_GetObjectItem(ccfg, "ReconnectInterval");
        cJSON *rn = cJSON_GetObjectItem(ccfg, "ReconnectNonce");
        if (pi && cJSON_IsNumber(pi)) out->ping_interval_ms = pi->valueint * 1000;
        if (ri && cJSON_IsNumber(ri)) out->reconnect_interval_ms = ri->valueint * 1000;
        if (rn && cJSON_IsNumber(rn)) out->reconnect_nonce_ms = rn->valueint * 1000;
    }

    cJSON_Delete(root);
    pr_info("WS config ready: service_id=%d ping=%dms", out->service_id, out->ping_interval_ms);
    return 0;
}



static void feishu_trim_ascii_in_place(char *s)
{
    if (!s || !s[0]) {
        return;
    }

    char *start = s;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') {
        start++;
    }
    if (start != s) {
        memmove(s, start, strlen(start) + 1);
    }

    size_t len = strlen(s);
    while (len > 0) {
        char ch = s[len - 1];
        if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') {
            break;
        }
        s[--len] = '\0';
    }
}

static size_t feishu_pick_chunk_len(const char *text, size_t max_bytes)
{
    size_t total = strlen(text);
    if (total <= max_bytes) {
        return total;
    }

    size_t cut = max_bytes;
    while (cut > 0 && (((unsigned char)text[cut]) & 0xC0) == 0x80) {
        cut--;
    }
    if (cut == 0) {
        cut = max_bytes;
    }

    size_t best = cut;
    for (size_t i = cut; i > cut / 2; --i) {
        if (text[i - 1] == '\n') {
            best = i;
            break;
        }
    }

    while (best > 0 && (text[best - 1] == '\n' || text[best - 1] == '\r')) {
        best--;
    }
    return best > 0 ? best : cut;
}

static char *feishu_build_card_content_json(const char *markdown)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *config = cJSON_CreateObject();
    cJSON *body = cJSON_CreateObject();
    cJSON *elements = cJSON_CreateArray();
    cJSON *markdown_el = cJSON_CreateObject();
    if (!root || !config || !body || !elements || !markdown_el) {
        cJSON_Delete(root);
        cJSON_Delete(config);
        cJSON_Delete(body);
        cJSON_Delete(elements);
        cJSON_Delete(markdown_el);
        return NULL;
    }

    cJSON_AddStringToObject(root, "schema", "2.0");
    cJSON_AddBoolToObject(config, "update_multi", 1);
    cJSON_AddItemToObject(root, "config", config);

    cJSON_AddStringToObject(markdown_el, "tag", "markdown");
    cJSON_AddStringToObject(markdown_el, "content", markdown ? markdown : "");
    cJSON_AddStringToObject(markdown_el, "text_align", "left");
    cJSON_AddStringToObject(markdown_el, "text_size", "normal");
    cJSON_AddStringToObject(markdown_el, "margin", "0px 0px 0px 0px");
    cJSON_AddItemToArray(elements, markdown_el);

    cJSON_AddItemToObject(body, "elements", elements);
    cJSON_AddItemToObject(root, "body", body);

    char *content_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return content_str;
}

static err_t feishu_send_payload_json(const char *token,
                                            const char *url,
                                            const char *receive_id,
                                            const char *msg_type,
                                            const char *content_str,
                                            const char *action_name)
{
    if (!token || !token[0] || !url || !msg_type || !content_str) {
        return ERR_INVALID_ARG;
    }

    cJSON *body = cJSON_CreateObject();
    if (!body) {
        return ERR_NO_MEM;
    }
    if (receive_id && receive_id[0]) {
        cJSON_AddStringToObject(body, "receive_id", receive_id);
    }
    cJSON_AddStringToObject(body, "msg_type", msg_type);
    cJSON_AddStringToObject(body, "content", content_str);

    char *json_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!json_str) {
        return ERR_NO_MEM;
    }

    feishu_http_response_t resp = {0};
    err_t err = feishu_http_post_json(url, token, json_str, TIMEOUT_MEDIUM, &resp);
    kfree(json_str);
    if (err != 0) {
        feishu_http_response_free(&resp);
        return err;
    }

    cJSON *root = feishu_http_parse_json(&resp);
    feishu_http_response_free(&resp);
    if (!root) {
        pr_err("%s request failed", action_name);
        return ERR_FAIL;
    }
    cJSON_Delete(root);
    return 0;
}

static err_t feishu_send_card_chunks(const char *token,
                                           const char *url,
                                           const char *receive_id,
                                           const char *text,
                                           const char *action_name)
{
    if (!token || !token[0] || !url || !text) {
        return ERR_INVALID_ARG;
    }

    const char *cursor = text;
    int part = 1;
    int all_ok = 1;
    while (*cursor) {
        while (*cursor == '\n' || *cursor == '\r') {
            cursor++;
        }
        if (!*cursor) {
            break;
        }

        size_t chunk_len = feishu_pick_chunk_len(cursor, FEISHU_CARD_CHUNK_MAX_BYTES);
        char *segment = kmalloc(chunk_len + 1, GFP_KERNEL);
        if (!segment) {
            return ERR_NO_MEM;
        }
        memcpy(segment, cursor, chunk_len);
        segment[chunk_len] = '\0';
        feishu_trim_ascii_in_place(segment);

        char *card_content = feishu_build_card_content_json(segment);
        kfree(segment);
        if (!card_content) {
            return ERR_NO_MEM;
        }

        err_t err = feishu_send_payload_json(token, url, receive_id, "interactive", card_content, action_name);
        kfree(card_content);
        if (err != 0) {
            all_ok = 0;
        } else {
            pr_info("%s ok (%s part %d)", action_name, receive_id ? receive_id : "reply", part);
        }

        cursor += chunk_len;
        part++;
    }

    return all_ok ? 0 : ERR_FAIL;
}

err_t feishu_api_send_card(const char *app_id,
                                 const char *app_secret,
                                 const char *chat_id,
                                 const char *markdown)
{
    if (!app_id || !app_id[0] || !app_secret || !app_secret[0] ||
        !chat_id || !markdown) {
        return ERR_INVALID_ARG;
    }

    char token[512];
    err_t token_err = feishu_api_get_tenant_token(app_id, app_secret, token, sizeof(token));
    if (token_err != 0) {
        return token_err;
    }

    const char *id_type = "chat_id";
    if (strncmp(chat_id, "ou_", 3) == 0) {
        id_type = "open_id";
    }

    char url[BUF_SMALL];
    snprintf(url, sizeof(url), "%s?receive_id_type=%s", FEISHU_SEND_MSG_URL, id_type);
    return feishu_send_card_chunks(token, url, chat_id, markdown, "Send card");
}

err_t feishu_api_reply_card(const char *app_id,
                                  const char *app_secret,
                                  const char *message_id,
                                  const char *markdown)
{
    if (!app_id || !app_id[0] || !app_secret || !app_secret[0] ||
        !message_id || !markdown) {
        return ERR_INVALID_ARG;
    }

    char token[512];
    err_t token_err = feishu_api_get_tenant_token(app_id, app_secret, token, sizeof(token));
    if (token_err != 0) {
        return token_err;
    }

    char url[BUF_SMALL];
    snprintf(url, sizeof(url), FEISHU_REPLY_MSG_URL, message_id);
    return feishu_send_card_chunks(token, url, NULL, markdown, "Reply card");
}
