#pragma once

#include <stddef.h>
#include "err.h"

#ifdef __cplusplus
extern "C" {
#endif

struct curl_slist;

typedef struct {
    long status;
    char *body;
    size_t body_len;
    char *headers;
    size_t headers_len;
    char *error;
} host_http_response_t;

daima_err_t host_http_request(const char *method,
                            const char *url,
                            struct curl_slist *headers,
                            const char *body,
                            int timeout_ms,
                            host_http_response_t *out);

void host_http_response_free(host_http_response_t *resp);

#ifdef __cplusplus
}
#endif

