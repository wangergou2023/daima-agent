/* 飞书 HTTP API——封装 libcurl 实现飞书 Open API 调用。
 * 提供 POST JSON 和 GET 两种方法，自动管理 Bearer Token 和 JSON 解析。 */

#include "drivers/channel/feishu/feishu_http.h"

#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>

#include "http.h"
#include "linux/printk.h"
#include "linux/slab.h"

/* 释放 HTTP 响应资源（body 由 kmalloc 分配）。 */
void feishu_http_response_free(feishu_http_response_t *resp)
{
    if (!resp) return;
    kfree(resp->body);
    resp->body = NULL;
    resp->status = 0;
}

/* 发送 HTTP 请求（POST/GET），自动添加 Bearer Token 和 Content-Type 头。 */
static err_t feishu_http_request(const char *url, const char *token,
                                        const char *method, const char *post_data,
                                        int timeout_ms, feishu_http_response_t *out)
{
    if (!url || !method || !out) {
        return ERR_INVALID_ARG;
    }

    struct curl_slist *headers = NULL;
    if (token && token[0]) {
        char auth_header[600];
        snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", token);
        headers = curl_slist_append(headers, auth_header);
    }
    headers = curl_slist_append(headers, "Content-Type: application/json; charset=utf-8");

    host_http_response_t resp = {0};
    err_t err = host_http_request(method, url, headers, post_data, timeout_ms, &resp);
    curl_slist_free_all(headers);

    if (err != 0) {
        host_http_response_free(&resp);
        return err;
    }

    out->status = resp.status;
    out->body = resp.body;
    kfree(resp.headers);
    resp.headers = NULL;
    return 0;
}

/* POST JSON 请求快捷封装。 */
err_t feishu_http_post_json(const char *url, const char *token,
                                   const char *json_body, int timeout_ms,
                                   feishu_http_response_t *out)
{
    return feishu_http_request(url, token, "POST", json_body, timeout_ms, out);
}

/* GET 请求快捷封装。 */
err_t feishu_http_get(const char *url, const char *token,
                             int timeout_ms, feishu_http_response_t *out)
{
    return feishu_http_request(url, token, "GET", NULL, timeout_ms, out);
}

/* 解析飞书 API JSON 响应，检查 code==0，失败时打印警告并返回 NULL。 */
cJSON *feishu_http_parse_json(feishu_http_response_t *resp)
{
    if (!resp || !resp->body) return NULL;
    cJSON *root = cJSON_Parse(resp->body);
    if (!root) {
        pr_warn("Failed to parse JSON response");
        return NULL;
    }
    cJSON *code = cJSON_GetObjectItem(root, "code");
    if (!code || !cJSON_IsNumber(code) || code->valueint != 0) {
        pr_warn("API error code=%d", code ? code->valueint : -1);
        cJSON_Delete(root);
        return NULL;
    }
    return root;
}
