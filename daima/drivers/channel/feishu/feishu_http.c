#include "drivers/channel/feishu/feishu_http.h"

#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>

#include "http.h"
#include "linux/printk.h"
#include "linux/slab.h"

static const char *TAG = "feishu_http";

void feishu_http_response_free(feishu_http_response_t *resp)
{
    if (!resp) return;
    kfree(resp->body);
    resp->body = NULL;
    resp->status = 0;
}

static daima_err_t feishu_http_request(const char *url, const char *token,
                                        const char *method, const char *post_data,
                                        int timeout_ms, feishu_http_response_t *out)
{
    if (!url || !method || !out) {
        return DAIMA_ERR_INVALID_ARG;
    }

    struct curl_slist *headers = NULL;
    if (token && token[0]) {
        char auth_header[600];
        snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", token);
        headers = curl_slist_append(headers, auth_header);
    }
    headers = curl_slist_append(headers, "Content-Type: application/json; charset=utf-8");

    host_http_response_t resp = {0};
    daima_err_t err = host_http_request(method, url, headers, post_data, timeout_ms, &resp);
    curl_slist_free_all(headers);

    if (err != DAIMA_OK) {
        host_http_response_free(&resp);
        return err;
    }

    out->status = resp.status;
    out->body = resp.body;
    kfree(resp.headers);
    resp.headers = NULL;
    return DAIMA_OK;
}

daima_err_t feishu_http_post_json(const char *url, const char *token,
                                   const char *json_body, int timeout_ms,
                                   feishu_http_response_t *out)
{
    return feishu_http_request(url, token, "POST", json_body, timeout_ms, out);
}

daima_err_t feishu_http_get(const char *url, const char *token,
                             int timeout_ms, feishu_http_response_t *out)
{
    return feishu_http_request(url, token, "GET", NULL, timeout_ms, out);
}

cJSON *feishu_http_parse_json(feishu_http_response_t *resp)
{
    if (!resp || !resp->body) return NULL;
    cJSON *root = cJSON_Parse(resp->body);
    if (!root) {
        DAIMA_LOGW(TAG, "Failed to parse JSON response");
        return NULL;
    }
    cJSON *code = cJSON_GetObjectItem(root, "code");
    if (!code || !cJSON_IsNumber(code) || code->valueint != 0) {
        DAIMA_LOGW(TAG, "API error code=%d", code ? code->valueint : -1);
        cJSON_Delete(root);
        return NULL;
    }
    return root;
}
