#include "arch/host/llm_http_client_host.h"

#include <curl/curl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "http.h"
#include "tls.h"
#include "autoconf.h"
#include "linux/printk.h"
#include "proxy.h"
#include "linux/slab.h"

#define LLM_DUMP_MAX_BYTES   (16 * 1024)
#define LLM_DUMP_CHUNK_BYTES 320
#define LLM_HTTP_AUTH_HEADER_MAX 352

typedef struct {
    char *data;
    size_t len;
} llm_http_buf_t;

struct llm_async_request {
    CURL *easy;
    llm_http_buf_t body;
    struct curl_slist *headers;
    bool owns_headers;
    bool completed;
    bool removed_from_multi;
    CURLcode result;
    long status;
};

static CURLM *s_llm_multi = NULL;

static size_t llm_http_write_cb(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    llm_http_buf_t *buf = (llm_http_buf_t *)userp;
    char *ptr = realloc(buf->data, buf->len + realsize + 1);
    if (!ptr) {
        return 0;
    }
    buf->data = ptr;
    memcpy(buf->data + buf->len, contents, realsize);
    buf->len += realsize;
    buf->data[buf->len] = '\0';
    return realsize;
}

static void llm_http_multi_init_once(void)
{
    if (s_llm_multi) {
        return;
    }
    curl_global_init(CURL_GLOBAL_DEFAULT);
    s_llm_multi = curl_multi_init();
}

static void llm_http_apply_proxy(CURL *curl)
{
    if (!http_proxy_is_enabled()) {
        return;
    }

    const char *host = http_proxy_host();
    uint16_t port = http_proxy_port();
    const char *type = http_proxy_type();
    if (!host || !host[0] || port == 0) {
        return;
    }

    char proxy[256];
    const char *scheme = "http";
    if (type && strcmp(type, "socks5") == 0) {
        scheme = "socks5h";
    }
    snprintf(proxy, sizeof(proxy), "%s://%s:%u", scheme, host, port);

    curl_easy_setopt(curl, CURLOPT_PROXY, proxy);
    if (type && strcmp(type, "socks5") == 0) {
#ifdef CURLPROXY_SOCKS5_HOSTNAME
        curl_easy_setopt(curl, CURLOPT_PROXYTYPE, CURLPROXY_SOCKS5_HOSTNAME);
#else
        curl_easy_setopt(curl, CURLOPT_PROXYTYPE, CURLPROXY_SOCKS5);
#endif
    } else {
        curl_easy_setopt(curl, CURLOPT_PROXYTYPE, CURLPROXY_HTTP);
    }
}

static void llm_http_async_mark_completed(llm_async_request_t *req, CURLcode result)
{
    if (!req || req->completed) {
        return;
    }
    req->completed = true;
    req->result = result;
    if (req->easy) {
        curl_easy_getinfo(req->easy, CURLINFO_RESPONSE_CODE, &req->status);
    }
}

static void llm_http_async_poll_multi(void)
{
    if (!s_llm_multi) {
        return;
    }

    int running = 0;
    CURLMcode mcode = curl_multi_perform(s_llm_multi, &running);
    (void)mcode;

    int msgs_left = 0;
    CURLMsg *msg = NULL;
    while ((msg = curl_multi_info_read(s_llm_multi, &msgs_left)) != NULL) {
        if (msg->msg != CURLMSG_DONE) {
            continue;
        }

        llm_async_request_t *req = NULL;
        curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &req);
        llm_http_async_mark_completed(req, msg->data.result);
    }
}

llm_async_request_t *llm_http_async_request(const char *method,
                                            const char *url,
                                            struct curl_slist *headers,
                                            const char *body,
                                            int timeout_ms)
{
    if (!method || !url) {
        return NULL;
    }

    llm_http_multi_init_once();
    if (!s_llm_multi) {
        return NULL;
    }

    llm_async_request_t *req = kzalloc(sizeof(*req), GFP_KERNEL);
    if (!req) {
        return NULL;
    }

    req->easy = curl_easy_init();
    if (!req->easy) {
        kfree(req);
        return NULL;
    }

    req->headers = headers;
    req->result = CURLE_OK;

    curl_easy_setopt(req->easy, CURLOPT_URL, url);
    curl_easy_setopt(req->easy, CURLOPT_CUSTOMREQUEST, method);
    curl_easy_setopt(req->easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(req->easy, CURLOPT_TIMEOUT_MS, timeout_ms);
    curl_easy_setopt(req->easy, CURLOPT_WRITEFUNCTION, llm_http_write_cb);
    curl_easy_setopt(req->easy, CURLOPT_WRITEDATA, &req->body);
    curl_easy_setopt(req->easy, CURLOPT_USERAGENT, "daima-host/0.1");
    curl_easy_setopt(req->easy, CURLOPT_PRIVATE, req);

    if (headers) {
        curl_easy_setopt(req->easy, CURLOPT_HTTPHEADER, headers);
    }

    if (body) {
        curl_easy_setopt(req->easy, CURLOPT_POSTFIELDS, body);
        curl_easy_setopt(req->easy, CURLOPT_POSTFIELDSIZE, (long)strlen(body));
    }

    if (strcmp(method, "HEAD") == 0) {
        curl_easy_setopt(req->easy, CURLOPT_NOBODY, 1L);
    }

    llm_http_apply_proxy(req->easy);
    host_tls_apply_curl_ca(req->easy);

    CURLMcode mcode = curl_multi_add_handle(s_llm_multi, req->easy);
    if (mcode != CURLM_OK) {
        curl_easy_cleanup(req->easy);
        kfree(req);
        return NULL;
    }

    llm_http_async_poll_multi();
    return req;
}

bool llm_http_async_is_done(llm_async_request_t *req)
{
    if (!req) {
        return true;
    }
    if (!req->completed) {
        llm_http_async_poll_multi();
    }
    return req->completed;
}

daima_err_t llm_http_async_get_response(llm_async_request_t *req,
                                        char **out_body,
                                        long *out_status)
{
    if (!req || !out_body || !out_status) {
        return DAIMA_ERR_INVALID_ARG;
    }

    *out_body = NULL;
    *out_status = 0;

    while (!llm_http_async_is_done(req)) {
        if (s_llm_multi) {
            curl_multi_poll(s_llm_multi, NULL, 0, 50, NULL);
        } else {
            usleep(50 * 1000);
        }
    }

    *out_status = req->status;
    if (req->result != CURLE_OK) {
        return DAIMA_FAIL;
    }

    const char *body = req->body.data ? req->body.data : "";
    *out_body = strdup(body);
    if (!*out_body) {
        return DAIMA_ERR_NO_MEM;
    }

    return DAIMA_OK;
}

void llm_http_async_free(llm_async_request_t *req)
{
    if (!req) {
        return;
    }

    if (req->easy) {
        if (s_llm_multi && !req->removed_from_multi) {
            curl_multi_remove_handle(s_llm_multi, req->easy);
            req->removed_from_multi = true;
        }
        curl_easy_cleanup(req->easy);
    }
    if (req->owns_headers && req->headers) {
        curl_slist_free_all(req->headers);
    }
    kfree(req->body.data);
    kfree(req);
}

void llm_http_log_payload(const char *tag, const char *label, const char *payload)
{
    if (!payload) {
        pr_info("%s: <null>", label);
        return;
    }

    size_t total = strlen(payload);
#if DAIMA_LLM_LOG_VERBOSE_PAYLOAD
    size_t shown = total > LLM_DUMP_MAX_BYTES ? LLM_DUMP_MAX_BYTES : total;
    pr_info("%s (%u bytes)%s", label, (unsigned)total, (shown < total) ? " [truncated]" : "");

    char chunk[LLM_DUMP_CHUNK_BYTES + 1];
    for (size_t off = 0; off < shown; off += LLM_DUMP_CHUNK_BYTES) {
        size_t n = shown - off;
        if (n > LLM_DUMP_CHUNK_BYTES) {
            n = LLM_DUMP_CHUNK_BYTES;
        }
        memcpy(chunk, payload + off, n);
        chunk[n] = '\0';
        pr_info("%s[%u]: %s", label, (unsigned)off, chunk);
    }
#else
    if (DAIMA_LLM_LOG_PREVIEW_BYTES > 0) {
        size_t shown = total > DAIMA_LLM_LOG_PREVIEW_BYTES ? DAIMA_LLM_LOG_PREVIEW_BYTES : total;
        char preview[DAIMA_LLM_LOG_PREVIEW_BYTES + 1];
        memcpy(preview, payload, shown);
        preview[shown] = '\0';
        for (size_t i = 0; i < shown; i++) {
            if (preview[i] == '\n' || preview[i] == '\r' || preview[i] == '\t') {
                preview[i] = ' ';
            }
        }
        pr_info("%s (%u bytes): %s%s", label, (unsigned)total, preview, (shown < total) ? " ..." : "");
    } else {
        pr_info("%s (%u bytes)", label, (unsigned)total);
    }
#endif
}

daima_err_t llm_http_post_json(const char *url,
                              const char *api_key,
                              const char *post_data,
                              int timeout_ms,
                              char **body_out,
                              int *status_out)
{
    if (!url || !post_data || !body_out || !status_out) {
        return DAIMA_ERR_INVALID_ARG;
    }

    *body_out = NULL;
    *status_out = 0;

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    if (api_key && api_key[0] && url && strstr(url, "/anthropic/")) {
        char key_header[LLM_HTTP_AUTH_HEADER_MAX];
        snprintf(key_header, sizeof(key_header), "x-api-key: %s", api_key);
        headers = curl_slist_append(headers, key_header);
        headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");
    } else if (api_key && api_key[0]) {
        char auth[LLM_HTTP_AUTH_HEADER_MAX];
        snprintf(auth, sizeof(auth), "Authorization: Bearer %s", api_key);
        headers = curl_slist_append(headers, auth);
    }

    host_http_response_t resp = {0};
    daima_err_t err = host_http_request("POST", url, headers, post_data, timeout_ms, &resp);
    if (headers) {
        curl_slist_free_all(headers);
    }
    if (err != DAIMA_OK) {
        host_http_response_free(&resp);
        return err;
    }

    *status_out = (int)resp.status;
    if (resp.body) {
        *body_out = strdup(resp.body);
        if (!*body_out) {
            host_http_response_free(&resp);
            return DAIMA_ERR_NO_MEM;
        }
    } else {
        *body_out = strdup("");
        if (!*body_out) {
            host_http_response_free(&resp);
            return DAIMA_ERR_NO_MEM;
        }
    }

    host_http_response_free(&resp);
    return DAIMA_OK;
}
