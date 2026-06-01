#pragma once

#include <stddef.h>

#include "daima_err.h"

void llm_http_log_payload(const char *tag, const char *label, const char *payload);

daima_err_t llm_http_post_json(const char *url,
                              const char *api_key,
                              const char *post_data,
                              int timeout_ms,
                              char **body_out,
                              int *status_out);
