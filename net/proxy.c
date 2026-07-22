/* HTTP 代理实现：从 autoconf 加载代理配置，支持 HTTP 和 SOCKS5。 */

#include "proxy.h"
#include "autoconf.h"

#include <string.h>
#include <stdlib.h>
#include "linux/printk.h"

/* 代理配置（进程级静态变量） */
static char s_proxy_host[64] = {0};          /* 代理主机 */
static uint16_t s_proxy_port = 0;           /* 代理端口 */
static char s_proxy_type[8] = "http";       /* 代理类型：http 或 socks5 */

/**
 * 从 autoconf 的 SECRET_PROXY_* 宏加载代理配置。
 * @return 始终返回 0
 */
err_t http_proxy_init(void)
{
    if (SECRET_PROXY_HOST[0] != '\0' && SECRET_PROXY_PORT[0] != '\0') {
        strncpy(s_proxy_host, SECRET_PROXY_HOST, sizeof(s_proxy_host) - 1);
        s_proxy_port = (uint16_t)atoi(SECRET_PROXY_PORT);
        if (SECRET_PROXY_TYPE[0] != '\0') {
            strncpy(s_proxy_type, SECRET_PROXY_TYPE, sizeof(s_proxy_type) - 1);
        }
    }

    if (s_proxy_host[0] && s_proxy_port) {
        pr_info("Proxy configured: %s:%d (%s)", s_proxy_host, s_proxy_port, s_proxy_type);
    } else {
        pr_info("No proxy configured (direct connection)");
    }
    return 0;
}

/** 代理是否已配置。 */
bool http_proxy_is_enabled(void)
{
    return s_proxy_host[0] != '\0' && s_proxy_port != 0;
}

const char *http_proxy_host(void)
{
    return s_proxy_host;
}

uint16_t http_proxy_port(void)
{
    return s_proxy_port;
}

const char *http_proxy_type(void)
{
    return s_proxy_type;
}
