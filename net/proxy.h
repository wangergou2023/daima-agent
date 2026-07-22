/* HTTP 代理配置接口。 */

#pragma once

#include "err.h"
#include <stddef.h>
#include <stdbool.h>

/**
 * 初始化代理模块。
 */
err_t http_proxy_init(void);

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
