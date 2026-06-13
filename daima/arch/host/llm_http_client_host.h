#pragma once

#include <stddef.h>
#include <stdbool.h>

#include <curl/curl.h>

#include "err.h"

void llm_http_log_payload(const char *tag, const char *label, const char *payload);

typedef struct llm_async_request llm_async_request_t;

llm_async_request_t *llm_http_async_request(const char *method,
                                            const char *url,
                                            struct curl_slist *headers,
                                            const char *body,
                                            int timeout_ms);

bool llm_http_async_is_done(llm_async_request_t *req);

daima_err_t llm_http_async_get_response(llm_async_request_t *req,
                                        char **out_body,
                                        long *out_status);

void llm_http_async_free(llm_async_request_t *req);

daima_err_t llm_http_post_json(const char *url,
                              const char *api_key,
                              const char *post_data,
                              int timeout_ms,
                              char **body_out,
                              int *status_out);
