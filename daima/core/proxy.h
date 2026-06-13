/* HTTP 代理配置接口。 */

#pragma once

#include "core/err.h"
#include <stddef.h>
#include <stdbool.h>

/**
 * 初始化代理模块。
 */
daima_err_t http_proxy_init(void);

/**
 * 若已配置代理 host:port，则返回 true。
 */
bool http_proxy_is_enabled(void);

/**
 * 获取代理主机（若已配置）。
 */
const char *http_proxy_host(void);

/**
 * 获取代理端口（若已配置）。
 */
uint16_t http_proxy_port(void);

/**
 * 获取代理类型（"http" 或 "socks5"）。
 */
const char *http_proxy_type(void);

/**
 * 设置代理 host、port 和 type（仅进程内生效）。
 */
daima_err_t http_proxy_set(const char *host, uint16_t port, const char *type);

/**
 * 清空代理配置。
 */
daima_err_t http_proxy_clear(void);

/* ── 经代理的 HTTPS 连接 ─────────────────────────────────── */

typedef struct proxy_conn proxy_conn_t;

/**
 * 通过已配置代理打开 HTTPS 连接。
 * 1) TCP 连接到代理
 * 2) 发送 HTTP CONNECT 到目标 host:port
 * 3) 在隧道上进行 TLS 握手
 *
 * 失败时返回 NULL。
 */
proxy_conn_t *proxy_conn_open(const char *host, int port, int timeout_ms);

/** 通过 TLS 隧道写入原始字节。返回写入字节数或 -1。 */
int proxy_conn_write(proxy_conn_t *conn, const char *data, int len);

/** 从 TLS 隧道读取原始字节。返回读取字节数或 -1。 */
int proxy_conn_read(proxy_conn_t *conn, char *buf, int len, int timeout_ms);

/** 关闭并释放连接。 */
void proxy_conn_close(proxy_conn_t *conn);
