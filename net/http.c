/* HTTP 客户端：基于 libcurl 的 HTTP GET/POST/HEAD 请求封装，支持代理、TLS、取消令牌。 */

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
#include "linux/slab.h"

/* libcurl 全局初始化一次性保护 */
static pthread_once_t s_curl_once = PTHREAD_ONCE_INIT;

static void curl_global_init_once(void)
{
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

/* 动态缓冲区：用于 curl 写回调累积数据 */
typedef struct {
    char *data;    /* 累积数据指针（realloc 管理） */
    size_t len;    /* 累积数据长度 */
} buf_t;

/**
 * libcurl 响应体写回调。将接收数据追加到动态缓冲区。
 * @param contents 接收数据指针
 * @param size     每个元素大小
 * @param nmemb    元素个数
 * @param userp    buf_t 指针
 * @return 实际写入字节数，0 表示失败
 */
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

/**
 * libcurl 响应头写回调。
 */
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

/**
 * libcurl 进度回调：检查取消令牌，支持协作式中断。
 * @return 1 中断传输，0 继续
 */
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

/**
 * 若已配置代理，将其应用到 curl 句柄。
 * 支持 HTTP 和 SOCKS5 代理类型。
 * @param curl CURL 句柄
 */
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
        scheme = "socks5h";  /* socks5h 在代理端解析 DNS */
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

/**
 * 执行 HTTP 请求。支持 GET/POST/HEAD 方法，自动跟随重定向，支持超时和取消。
 * @param method     HTTP 方法（"GET"/"POST"/"HEAD"）
 * @param url        请求 URL
 * @param headers    curl_slist 请求头链表（可为 NULL）
 * @param body       请求体（POST 时使用，可为 NULL）
 * @param timeout_ms 超时毫秒数
 * @param out        输出：响应结构体（调用方需用 host_http_response_free 释放）
 * @return 成功返回 0，失败返回 ERR_*
 */
err_t host_http_request(const char *method,
                            const char *url,
                            struct curl_slist *headers,
                            const char *body,
                            int timeout_ms,
                            host_http_response_t *out)
{
    if (unlikely(!method || !url || !out)) return ERR_INVALID_ARG;

    pthread_once(&s_curl_once, curl_global_init_once);

    CURL *curl = curl_easy_init();
    if (unlikely(!curl)) return ERR_FAIL;

    buf_t resp = {0};
    buf_t hdrs = {0};

    /* 设置 curl 选项 */
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);     /* 跟随 3xx 重定向 */
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &hdrs);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "agent-host/0.1");
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);         /* 启用进度回调 */
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, cancel_progress_cb);

    if (headers) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }

    if (body) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(body));
    }

    /* HEAD 请求不下载响应体 */
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
            pr_info("HTTP request aborted by agent cancellation");
        } else {
            pr_warn("HTTP request failed: %s", curl_easy_strerror(res));
        }
        out->error = strdup(curl_easy_strerror(res));
        kfree(resp.data);
        kfree(hdrs.data);
        return ERR_FAIL;
    }

    /* 成功：填充响应结构体 */
    out->status = status;
    out->body = resp.data;
    out->body_len = resp.len;
    out->headers = hdrs.data;
    out->headers_len = hdrs.len;
    return 0;
}

/**
 * 释放 host_http_response_t 结构体内存。
 * @param resp 响应结构体指针
 */
void host_http_response_free(host_http_response_t *resp)
{
    if (unlikely(!resp)) return;
    kfree(resp->body);
    kfree(resp->headers);
    kfree(resp->error);
    resp->body = NULL;
    resp->headers = NULL;
    resp->error = NULL;
    resp->body_len = 0;
    resp->headers_len = 0;
    resp->status = 0;
}
