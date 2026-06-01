#pragma once

#include <curl/curl.h>
#include <openssl/ssl.h>

const char *host_tls_ca_cert_path(void);
void host_tls_apply_curl_ca(CURL *curl);
void host_tls_apply_ssl_ctx_ca(SSL_CTX *ctx);
