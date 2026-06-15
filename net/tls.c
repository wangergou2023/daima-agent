#include "tls.h"

#include <unistd.h>

#include "paths.h"
#include "env.h"
#include "linux/printk.h"
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

void host_tls_apply_curl_ca(CURL *curl)
{
    const char *ca = host_tls_ca_cert_path();
    if (curl && ca && ca[0]) {
        curl_easy_setopt(curl, CURLOPT_CAINFO, ca);
    }
}

void host_tls_apply_ssl_ctx_ca(SSL_CTX *ctx)
{
    const char *ca = host_tls_ca_cert_path();
    if (ctx && ca && ca[0]) {
        if (SSL_CTX_load_verify_locations(ctx, ca, NULL) == 1) {
            return;
        }
        pr_warn("Failed to load CA cert from %s", ca);
    }
    SSL_CTX_set_default_verify_paths(ctx);
}
