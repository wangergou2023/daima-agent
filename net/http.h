/* HTTP 客户端接口：请求/响应类型定义与函数声明。 */

#pragma once

#include <stddef.h>
#include "err.h"

#ifdef __cplusplus
extern "C" {
#endif

struct curl_slist;

/* HTTP 响应结构体 */
typedef struct {
    long status;        /* HTTP 状态码 */
    char *body;         /* 响应体（调用方需释放） */
    size_t body_len;    /* 响应体长度 */
    char *headers;      /* 响应头（调用方需释放） */
    size_t headers_len; /* 响应头长度 */
    char *error;        /* 错误信息（仅在请求失败时有值） */
} host_http_response_t;

/**
 * 执行 HTTP 请求。支持 GET/POST/HEAD，自动跟随重定向。
 * @param method     HTTP 方法
 * @param url        请求 URL
 * @param headers    curl_slist 请求头链表（可为 NULL）
 * @param body       请求体（可为 NULL）
 * @param timeout_ms 超时毫秒数
 * @param out        输出响应（调用方需用 host_http_response_free 释放）
 * @return 成功返回 0
 */
err_t host_http_request(const char *method,
                            const char *url,
                            struct curl_slist *headers,
                            const char *body,
                            int timeout_ms,
                            host_http_response_t *out);

/** 释放 host_http_response_t 内部分配的内存。 */
void host_http_response_free(host_http_response_t *resp);

#ifdef __cplusplus
}
#endif

