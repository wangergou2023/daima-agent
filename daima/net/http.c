#include "http.h"
#include "cancel.h"
#include "tls.h"
#include "proxy.h"
#include "autoconf.h"
#include "linux/compiler.h"
#include "linux/printk.h"

#include <curl/curl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *TAG = "http";
static pthread_once_t s_curl_once = PTHREAD_ONCE_INIT;

static void curl_global_init_once(void)
{
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

typedef struct {
    char *data;
    size_t len;
} buf_t;

static size_t write_cb(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    buf_t *buf = (buf_t *)userp;
    char *ptr = realloc(buf->data, buf->len + realsize + 1);
    if (unlikely(!ptr)) return 0;
    buf->data = ptr;
    memcpy(&(buf->data[buf->len]), contents, realsize);
    buf->len += realsize;
    buf->data[buf->len] = '\0';
    return realsize;
}

static size_t header_cb(char *buffer, size_t size, size_t nitems, void *userp)
{
    size_t realsize = size * nitems;
    buf_t *buf = (buf_t *)userp;
    char *ptr = realloc(buf->data, buf->len + realsize + 1);
    if (unlikely(!ptr)) return 0;
    buf->data = ptr;
    memcpy(&(buf->data[buf->len]), buffer, realsize);
    buf->len += realsize;
    buf->data[buf->len] = '\0';
    return realsize;
}

static int cancel_progress_cb(void *clientp,
                              curl_off_t dltotal,
                              curl_off_t dlnow,
                              curl_off_t ultotal,
                              curl_off_t ulnow)
{
    (void)clientp;
    (void)dltotal;
    (void)dlnow;
    (void)ultotal;
    (void)ulnow;
    return agent_cancel_current_thread_cancelled() ? 1 : 0;
}

static void apply_proxy(CURL *curl)
{
    if (!http_proxy_is_enabled()) return;

    const char *host = http_proxy_host();
    uint16_t port = http_proxy_port();
    const char *type = http_proxy_type();
    if (unlikely(!host || !host[0] || port == 0)) return;

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

daima_err_t host_http_request(const char *method,
                            const char *url,
                            struct curl_slist *headers,
                            const char *body,
                            int timeout_ms,
                            host_http_response_t *out)
{
    if (unlikely(!method || !url || !out)) return DAIMA_ERR_INVALID_ARG;

    pthread_once(&s_curl_once, curl_global_init_once);

    CURL *curl = curl_easy_init();
    if (unlikely(!curl)) return DAIMA_FAIL;

    buf_t resp = {0};
    buf_t hdrs = {0};

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &hdrs);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "daima-host/0.1");
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, cancel_progress_cb);

    if (headers) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }

    if (body) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(body));
    }

    if (strcmp(method, "HEAD") == 0) {
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    }

    apply_proxy(curl);
    host_tls_apply_curl_ca(curl);

    CURLcode res = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);

    if (unlikely(res != CURLE_OK)) {
        if (res == CURLE_ABORTED_BY_CALLBACK && agent_cancel_current_thread_cancelled()) {
            DAIMA_LOGI(TAG, "HTTP request aborted by agent cancellation");
        } else {
            DAIMA_LOGW(TAG, "HTTP request failed: %s", curl_easy_strerror(res));
        }
        out->error = strdup(curl_easy_strerror(res));
        free(resp.data);
        free(hdrs.data);
        return DAIMA_FAIL;
    }

    out->status = status;
    out->body = resp.data;
    out->body_len = resp.len;
    out->headers = hdrs.data;
    out->headers_len = hdrs.len;
    return DAIMA_OK;
}

void host_http_response_free(host_http_response_t *resp)
{
    if (unlikely(!resp)) return;
    free(resp->body);
    free(resp->headers);
    free(resp->error);
    resp->body = NULL;
    resp->headers = NULL;
    resp->error = NULL;
    resp->body_len = 0;
    resp->headers_len = 0;
    resp->status = 0;
}
