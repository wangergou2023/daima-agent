/* TLS/SSL CA 证书管理接口。 */

#pragma once

#include <curl/curl.h>
#include <openssl/ssl.h>

/** 查找 CA 证书路径（环境变量 → 内置路径）。@return 可读路径或 NULL */
const char *host_tls_ca_cert_path(void);

/** 将 CA 证书应用到 libcurl 句柄。@param curl CURL 句柄 */
void host_tls_apply_curl_ca(CURL *curl);

/** 将 CA 证书应用到 OpenSSL SSL_CTX。@param ctx SSL_CTX 句柄 */
void host_tls_apply_ssl_ctx_ca(SSL_CTX *ctx);
