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

/** 运行时设置代理。 */
err_t http_proxy_set(const char *host, uint16_t port, const char *type)
{
    strncpy(s_proxy_host, host, sizeof(s_proxy_host) - 1);
    s_proxy_port = port;
    strncpy(s_proxy_type, type, sizeof(s_proxy_type) - 1);
    pr_info("Proxy set to %s:%d (%s)", s_proxy_host, s_proxy_port, s_proxy_type);
    return 0;
}

/** 清空代理配置。 */
err_t http_proxy_clear(void)
{
    s_proxy_host[0] = '\0';
    s_proxy_port = 0;
    strcpy(s_proxy_type, "http");
    pr_info("Proxy cleared");
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

/* 代理 TLS 隧道在 host 模式下未使用 */
struct proxy_conn { int unused; };

proxy_conn_t *proxy_conn_open(const char *host, int port, int timeout_ms)
{
    (void)host; (void)port; (void)timeout_ms;
    pr_warn("proxy_conn_open not supported in host mode");
    return NULL;
}

int proxy_conn_write(proxy_conn_t *conn, const char *data, int len)
{
    (void)conn; (void)data; (void)len;
    return -1;
}

int proxy_conn_read(proxy_conn_t *conn, char *buf, int len, int timeout_ms)
{
    (void)conn; (void)buf; (void)len; (void)timeout_ms;
    return -1;
}

void proxy_conn_close(proxy_conn_t *conn)
{
    (void)conn;
}
