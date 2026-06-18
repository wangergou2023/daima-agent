/* LLM HTTP 客户端封装（host 平台 libcurl 实现）。
 * - 提供同步 POST JSON 和异步 HTTP 请求两种模式
 * - 统管 HTTP 连接复用（curl_multi）、代理、TLS 证书
 * - 支持 Anthropic 认证头（x-api-key + anthropic-version）和 OpenAI 认证头（Authorization: Bearer）
 */

#pragma once

#include <stddef.h>
#include <stdbool.h>

#include <curl/curl.h>

#include "err.h"

/**
 * 记录 LLM 请求/响应的 HTTP 载荷日志。
 * 根据 LLM_LOG_VERBOSE_PAYLOAD 宏选择完整输出或截断预览。
 *
 * @param tag     日志标签（如 "llm"）
 * @param label   载荷描述（如 "LLM tools request"）
 * @param payload HTTP 载荷内容；NULL 时输出 <null>
 */
void llm_http_log_payload(const char *tag, const char *label, const char *payload);

/* 异步请求不透明句柄（内部持有 CURL 句柄和响应缓冲区） */
typedef struct llm_async_request llm_async_request_t;

/**
 * 发起异步 HTTP 请求。
 * 内部通过 curl_multi 管理并发，请求在后台 I/O 事件循环中执行。
 *
 * @param method     HTTP 方法（如 "POST"）
 * @param url        请求 URL
 * @param headers    自定义 HTTP 头链表（调用者不再拥有所有权，随请求释放）
 * @param body       请求体（POST 数据）
 * @param timeout_ms 超时时间（毫秒）
 * @return           异步请求句柄；失败返回 NULL
 */
llm_async_request_t *llm_http_async_request(const char *method,
                                            const char *url,
                                            struct curl_slist *headers,
                                            const char *body,
                                            int timeout_ms);

/**
 * 检查异步请求是否已完成（非阻塞，零开销轮询）。
 * @param req  异步请求句柄
 * @return     true 表示已完成（可调用 get_response 获取结果）
 */
bool llm_http_async_is_done(llm_async_request_t *req);

/**
 * 获取异步请求的响应结果（阻塞直到完成）。
 * @param req        异步请求句柄
 * @param out_body   输出响应体（堆分配，调用者负责释放）
 * @param out_status 输出 HTTP 状态码
 * @return           成功返回 0；HTTP 传输失败或内存不足返回错误码
 */
err_t llm_http_async_get_response(llm_async_request_t *req,
                                        char **out_body,
                                        long *out_status);

/**
 * 释放异步请求资源。如果请求仍在进行中会取消。
 * @param req  异步请求句柄
 */
void llm_http_async_free(llm_async_request_t *req);

/**
 * 同步 POST JSON 请求。
 *
 * 自动设置 Content-Type 认证头：
 * - 包含 "/anthropic/" 的 URL → x-api-key + anthropic-version: 2023-06-01
 * - 其他 URL → Authorization: Bearer
 *
 * @param url        请求 URL
 * @param api_key    API 密钥（用于认证头）
 * @param post_data  JSON 请求体
 * @param timeout_ms 超时时间（毫秒）
 * @param body_out   输出响应体（堆分配，调用者负责释放）
 * @param status_out 输出 HTTP 状态码
 * @return           成功返回 0
 */
err_t llm_http_post_json(const char *url,
                              const char *api_key,
                              const char *post_data,
                              int timeout_ms,
                              char **body_out,
                              int *status_out);
