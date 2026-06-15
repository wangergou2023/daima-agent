#include "proxy.h"
#include "autoconf.h"

#include <string.h>
#include <stdlib.h>
#include "linux/printk.h"
static char s_proxy_host[64] = {0};
static uint16_t s_proxy_port = 0;
static char s_proxy_type[8] = "http";

daima_err_t http_proxy_init(void)
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
    return DAIMA_OK;
}

daima_err_t http_proxy_set(const char *host, uint16_t port, const char *type)
{
    strncpy(s_proxy_host, host, sizeof(s_proxy_host) - 1);
    s_proxy_port = port;
    strncpy(s_proxy_type, type, sizeof(s_proxy_type) - 1);
    pr_info("Proxy set to %s:%d (%s)", s_proxy_host, s_proxy_port, s_proxy_type);
    return DAIMA_OK;
}

daima_err_t http_proxy_clear(void)
{
    s_proxy_host[0] = '\0';
    s_proxy_port = 0;
    strcpy(s_proxy_type, "http");
    pr_info("Proxy cleared");
    return DAIMA_OK;
}

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

/* Proxy TLS tunnel is not used in host mode */
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
