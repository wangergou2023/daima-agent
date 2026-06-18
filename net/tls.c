/* TLS/SSL CA 证书管理：查找 CA 证书路径，应用到 libcurl 和 OpenSSL 上下文。 */

#include "tls.h"

#include <unistd.h>

#include "paths.h"
#include "env.h"
#include "linux/printk.h"

/**
 * 查找 CA 证书路径。查找顺序：
 *   1. 环境变量 CA_CERT_FILE
 *   2. 环境变量 CURL_CA_BUNDLE
 *   3. 环境变量 SSL_CERT_FILE
 *   4. 内置路径 path_ca_cert_file()
 * @return 可读的 CA 证书路径，找不到返回 NULL
 */
const char *host_tls_ca_cert_path(void)
{
    const char *ca = env_get("CA_CERT_FILE");
    if (!ca || !ca[0]) ca = getenv("CURL_CA_BUNDLE");
    if (!ca || !ca[0]) ca = getenv("SSL_CERT_FILE");
    if (ca && ca[0] && access(ca, R_OK) == 0) {
        return ca;
    }

    const char *fallback = path_ca_cert_file();
    if (fallback && fallback[0] && access(fallback, R_OK) == 0) {
        return fallback;
    }
    return NULL;
}

/**
 * 将 CA 证书应用到 libcurl 句柄。
 * @param curl CURL 句柄
 */
void host_tls_apply_curl_ca(CURL *curl)
{
    const char *ca = host_tls_ca_cert_path();
    if (curl && ca && ca[0]) {
        curl_easy_setopt(curl, CURLOPT_CAINFO, ca);
    }
}

/**
 * 将 CA 证书应用到 OpenSSL SSL_CTX 上下文。
 * 加载失败时回退到系统默认 CA 路径。
 * @param ctx OpenSSL SSL_CTX 句柄
 */
void host_tls_apply_ssl_ctx_ca(SSL_CTX *ctx)
{
    const char *ca = host_tls_ca_cert_path();
    if (ctx && ca && ca[0]) {
        if (SSL_CTX_load_verify_locations(ctx, ca, NULL) == 1) {
            return;
        }
        pr_warn("Failed to load CA cert from %s", ca);
    }
    /* 回退到系统默认 CA 路径 */
    SSL_CTX_set_default_verify_paths(ctx);
}
