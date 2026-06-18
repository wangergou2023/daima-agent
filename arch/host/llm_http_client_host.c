/* Host 平台 LLM HTTP 客户端实现（libcurl 封装）。
 *
 * 核心职责：
 * - 封装 libcurl 的 easy/multi 接口，提供同步阻塞和异步非阻塞两种 HTTP POST 模式
 * - 管理 curl_multi 全局句柄（单例），支持多请求并发
 * - 自动设置认证头：Anthropic（x-api-key）vs OpenAI（Authorization: Bearer）
 * - 代理集成（HTTP / SOCKS5）和 TLS 证书配置
 * - 响应载荷日志输出（完整/截断模式）
 *
 * HTTP 头自动设置：
 * - 所有请求：Content-Type: application/json
 * - Anthropic URL（含 /anthropic/）：x-api-key + anthropic-version: 2023-06-01
 * - OpenAI URL：Authorization: Bearer <api_key>
 */

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

/* 日志输出限制：完整模式 16KB，预览模式由 LLM_LOG_PREVIEW_BYTES 控制 */
#define LLM_DUMP_MAX_BYTES   (16 * 1024)
#define LLM_DUMP_CHUNK_BYTES 320
#define LLM_HTTP_AUTH_HEADER_MAX 352

/**
 * HTTP 响应缓冲区。
 * 通过 libcurl 的 CURLOPT_WRITEFUNCTION 回调累积数据。
 */
typedef struct {
	char *data;    /* realloc 分配的累积缓冲区 */
	size_t len;    /* 当前数据长度 */
} llm_http_buf_t;

/**
 * 异步 HTTP 请求内部结构。
 * 持有 CURL easy 句柄和响应缓冲区，通过 curl_multi 管理生命周期。
 */
struct llm_async_request {
	CURL *easy;                 /* curl easy 句柄 */
	llm_http_buf_t body;       /* 响应体缓冲区 */
	struct curl_slist *headers; /* HTTP 头链表 */
	bool owns_headers;          /* 是否拥有 headers 的所有权 */
	bool completed;             /* 请求是否已完成 */
	bool removed_from_multi;    /* 是否已从 curl_multi 移除 */
	CURLcode result;            /* libcurl 返回码 */
	long status;                /* HTTP 状态码 */
};

/* 全局 curl_multi 句柄（单例），所有异步请求共用 */
static CURLM *s_llm_multi = NULL;

/**
 * libcurl 写回调：将响应数据追加到 llm_http_buf_t。
 *
 * @param contents  响应数据块指针
 * @param size      每个元素大小
 * @param nmemb    元素数量
 * @param userp     llm_http_buf_t 指针
 * @return          实际处理的字节数；返回 0 表示错误（libcurl 会中止传输）
 */
static size_t llm_http_write_cb(void *contents, size_t size, size_t nmemb, void *userp)
{
	size_t realsize = size * nmemb;
	llm_http_buf_t *buf = (llm_http_buf_t *)userp;
	/* realloc 扩展缓冲区并追加数据 */
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

/**
 * 初始化全局 curl_multi 句柄（惰性初始化，仅首次调用生效）。
 * 同时调用 curl_global_init 进行全局 libcurl 初始化。
 */
static void llm_http_multi_init_once(void)
{
	if (s_llm_multi) {
		return;
	}
	curl_global_init(CURL_GLOBAL_DEFAULT);
	s_llm_multi = curl_multi_init();
}

/**
 * 将代理设置应用到 CURL 句柄。
 * 支持 HTTP 和 SOCKS5 代理类型。
 */
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
	/* SOCKS5 代理：使用 socks5h 使 DNS 解析也走代理 */
	if (type && strcmp(type, "socks5") == 0) {
		scheme = "socks5h";
	}
	snprintf(proxy, sizeof(proxy), "%s://%s:%u", scheme, host, port);

	curl_easy_setopt(curl, CURLOPT_PROXY, proxy);
	if (type && strcmp(type, "socks5") == 0) {
		/* CURLPROXY_SOCKS5_HOSTNAME 让代理解析 DNS（socks5h 语义） */
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
 * 标记异步请求完成并记录结果。
 *
 * @param req     异步请求句柄
 * @param result  libcurl 返回码
 */
static void llm_http_async_mark_completed(llm_async_request_t *req, CURLcode result)
{
	if (!req || req->completed) {
		return;
	}
	req->completed = true;
	req->result = result;
	if (req->easy) {
		/* 获取 HTTP 状态码 */
		curl_easy_getinfo(req->easy, CURLINFO_RESPONSE_CODE, &req->status);
	}
}

/**
 * 轮询 curl_multi 事件循环，处理完成的请求。
 * 遍历 curl_multi_info_read 消息队列，标记完成请求的状态。
 */
static void llm_http_async_poll_multi(void)
{
	if (!s_llm_multi) {
		return;
	}

	int running = 0;
	/* 驱动 libcurl 内部状态机 */
	CURLMcode mcode = curl_multi_perform(s_llm_multi, &running);
	(void)mcode;

	/* 读取完成消息队列 */
	int msgs_left = 0;
	CURLMsg *msg = NULL;
	while ((msg = curl_multi_info_read(s_llm_multi, &msgs_left)) != NULL) {
		if (msg->msg != CURLMSG_DONE) {
			continue;
		}

		/* 通过 CURLOPT_PRIVATE 取回 llm_async_request_t 指针 */
		llm_async_request_t *req = NULL;
		curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &req);
		llm_http_async_mark_completed(req, msg->data.result);
	}
}

/**
 * 发起异步 HTTP 请求。
 *
 * 设置 CURL 参数：
 * - 标准选项：URL / CUSTOMREQUEST / FOLLOWLOCATION / TIMEOUT_MS / USERAGENT
 * - 文件上传回调：CURLOPT_WRITEFUNCTION/CURLOPT_WRITEDATA
 * - 私有指针：CURLOPT_PRIVATE（用于完成回调时取回请求句柄）
 * - 代理和 TLS 配置
 * - 将 easy handle 添加到 curl_multi 并执行首轮轮询
 *
 * @param method     HTTP 方法
 * @param url        请求 URL
 * @param headers    自定义 HTTP 头链表（所有权转移）
 * @param body       请求体
 * @param timeout_ms 超时时间
 * @return           异步请求句柄；失败返回 NULL
 */
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

	/* 基本 CURL 参数 */
	curl_easy_setopt(req->easy, CURLOPT_URL, url);
	curl_easy_setopt(req->easy, CURLOPT_CUSTOMREQUEST, method);
	curl_easy_setopt(req->easy, CURLOPT_FOLLOWLOCATION, 1L);    /* 跟随重定向 */
	curl_easy_setopt(req->easy, CURLOPT_TIMEOUT_MS, timeout_ms);
	curl_easy_setopt(req->easy, CURLOPT_WRITEFUNCTION, llm_http_write_cb);
	curl_easy_setopt(req->easy, CURLOPT_WRITEDATA, &req->body);  /* 写回调上下文 */
	curl_easy_setopt(req->easy, CURLOPT_USERAGENT, "agent-host/0.1");
	curl_easy_setopt(req->easy, CURLOPT_PRIVATE, req);  /* 完成回调时取回请求句柄 */

	if (headers) {
		curl_easy_setopt(req->easy, CURLOPT_HTTPHEADER, headers);
	}

	if (body) {
		curl_easy_setopt(req->easy, CURLOPT_POSTFIELDS, body);
		curl_easy_setopt(req->easy, CURLOPT_POSTFIELDSIZE, (long)strlen(body));
	}

	/* HEAD 请求特殊处理：不返回 body */
	if (strcmp(method, "HEAD") == 0) {
		curl_easy_setopt(req->easy, CURLOPT_NOBODY, 1L);
	}

	/* 代理和 TLS 配置 */
	llm_http_apply_proxy(req->easy);
	host_tls_apply_curl_ca(req->easy);

	/* 注册到 curl_multi 并执行首轮 I/O */
	CURLMcode mcode = curl_multi_add_handle(s_llm_multi, req->easy);
	if (mcode != CURLM_OK) {
		curl_easy_cleanup(req->easy);
		kfree(req);
		return NULL;
	}

	llm_http_async_poll_multi();
	return req;
}

/**
 * 非阻塞检查异步请求是否完成。
 *
 * @param req  异步请求句柄
 * @return     true 表示已完成
 */
bool llm_http_async_is_done(llm_async_request_t *req)
{
	if (!req) {
		return true;
	}
	/* 未完成时执行一次 I/O 轮询 */
	if (!req->completed) {
		llm_http_async_poll_multi();
	}
	return req->completed;
}

/**
 * 获取异步请求结果（阻塞等待直到完成）。
 *
 * @param req        异步请求句柄
 * @param out_body   输出响应体（堆分配，调用者负责 free）
 * @param out_status 输出 HTTP 状态码
 * @return           成功返回 0
 */
err_t llm_http_async_get_response(llm_async_request_t *req,
                                        char **out_body,
                                        long *out_status)
{
	if (!req || !out_body || !out_status) {
		return ERR_INVALID_ARG;
	}

	*out_body = NULL;
	*out_status = 0;

	/* 轮询直到完成 */
	while (!llm_http_async_is_done(req)) {
		if (s_llm_multi) {
			/* 使用 curl_multi_poll 等待 I/O 事件（50ms 超时） */
			curl_multi_poll(s_llm_multi, NULL, 0, 50, NULL);
		} else {
			usleep(50 * 1000);
		}
	}

	*out_status = req->status;
	if (req->result != CURLE_OK) {
		return ERR_FAIL;
	}

	/* 复制响应体（调用者负责释放） */
	const char *body = req->body.data ? req->body.data : "";
	*out_body = strdup(body);
	if (!*out_body) {
		return ERR_NO_MEM;
	}

	return 0;
}

/**
 * 释放异步请求的所有资源。
 *
 * @param req  异步请求句柄
 */
void llm_http_async_free(llm_async_request_t *req)
{
	if (!req) {
		return;
	}

	if (req->easy) {
		/* 从 multi 句柄移除（仅一次） */
		if (s_llm_multi && !req->removed_from_multi) {
			curl_multi_remove_handle(s_llm_multi, req->easy);
			req->removed_from_multi = true;
		}
		curl_easy_cleanup(req->easy);
	}
	/* 仅当拥有 headers 所有权时释放 */
	if (req->owns_headers && req->headers) {
		curl_slist_free_all(req->headers);
	}
	kfree(req->body.data);
	kfree(req);
}

/**
 * 输出 LLM HTTP 载荷日志。
 *
 * 输出模式（由 LLM_LOG_VERBOSE_PAYLOAD 宏控制）：
 * - 完整模式：分段输出整个载荷（每段 LLM_DUMP_CHUNK_BYTES 字节，最大 LLM_DUMP_MAX_BYTES）
 * - 预览模式：输出前 LLM_LOG_PREVIEW_BYTES 字节并替换换行为空格
 * - 静默模式：仅输出总字节数
 *
 * @param tag     日志标签（未使用）
 * @param label   日志描述
 * @param payload HTTP 载荷内容
 */
void llm_http_log_payload(const char *tag, const char *label, const char *payload)
{
	if (!payload) {
		pr_info("%s: <null>", label);
		return;
	}

	size_t total = strlen(payload);
#if LLM_LOG_VERBOSE_PAYLOAD
	/* 完整模式：分段输出（最多 16KB） */
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
	/* 预览/静默模式 */
	if (LLM_LOG_PREVIEW_BYTES > 0) {
		/* 预览模式：输出前 N 字节，替换控制字符 */
		size_t shown = total > LLM_LOG_PREVIEW_BYTES ? LLM_LOG_PREVIEW_BYTES : total;
		char preview[LLM_LOG_PREVIEW_BYTES + 1];
		memcpy(preview, payload, shown);
		preview[shown] = '\0';
		for (size_t i = 0; i < shown; i++) {
			if (preview[i] == '\n' || preview[i] == '\r' || preview[i] == '\t') {
				preview[i] = ' ';
			}
		}
		pr_info("%s (%u bytes): %s%s", label, (unsigned)total, preview, (shown < total) ? " ..." : "");
	} else {
		/* 静默模式：仅输出总字节数 */
		pr_info("%s (%u bytes)", label, (unsigned)total);
	}
#endif
}

/**
 * 同步发送 POST JSON 请求。
 *
 * 自动构建 HTTP 头：
 * - Content-Type: application/json
 * - Anthropic URL（含 /anthropic/）：x-api-key + anthropic-version: 2023-06-01
 * - 其他 URL：Authorization: Bearer <api_key>
 *
 * @param url        请求 URL
 * @param api_key    API 密钥
 * @param post_data  JSON 请求体
 * @param timeout_ms 超时时间
 * @param body_out   输出响应体（堆分配，调用者负责 free）
 * @param status_out 输出 HTTP 状态码
 * @return           成功返回 0
 */
err_t llm_http_post_json(const char *url,
                              const char *api_key,
                              const char *post_data,
                              int timeout_ms,
                              char **body_out,
                              int *status_out)
{
	if (!url || !post_data || !body_out || !status_out) {
		return ERR_INVALID_ARG;
	}

	*body_out = NULL;
	*status_out = 0;

	/* 构建 HTTP 头链表 */
	struct curl_slist *headers = NULL;
	headers = curl_slist_append(headers, "Content-Type: application/json");

	/* Anthropic 协议头：x-api-key（非 Authorization: Bearer） */
	if (api_key && api_key[0] && url && strstr(url, "/anthropic/")) {
		char key_header[LLM_HTTP_AUTH_HEADER_MAX];
		snprintf(key_header, sizeof(key_header), "x-api-key: %s", api_key);
		headers = curl_slist_append(headers, key_header);
		headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");
	} else if (api_key && api_key[0]) {
		/* OpenAI 兼容协议头：Authorization: Bearer */
		char auth[LLM_HTTP_AUTH_HEADER_MAX];
		snprintf(auth, sizeof(auth), "Authorization: Bearer %s", api_key);
		headers = curl_slist_append(headers, auth);
	}

	/* 调用底层 host_http_request 发送同步请求 */
	host_http_response_t resp = {0};
	err_t err = host_http_request("POST", url, headers, post_data, timeout_ms, &resp);
	if (headers) {
		curl_slist_free_all(headers);
	}
	if (err != 0) {
		host_http_response_free(&resp);
		return err;
	}

	/* 提取响应状态码和 body */
	*status_out = (int)resp.status;
	if (resp.body) {
		*body_out = strdup(resp.body);
		if (!*body_out) {
			host_http_response_free(&resp);
			return ERR_NO_MEM;
		}
	} else {
		*body_out = strdup("");
		if (!*body_out) {
			host_http_response_free(&resp);
			return ERR_NO_MEM;
		}
	}

	host_http_response_free(&resp);
	return 0;
}
