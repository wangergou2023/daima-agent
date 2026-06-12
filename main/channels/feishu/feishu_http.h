#pragma once

#include "daima_err.h"
#include "cJSON.h"

typedef struct {
    long status;
    char *body;
} feishu_http_response_t;

void feishu_http_response_free(feishu_http_response_t *resp);
daima_err_t feishu_http_post_json(const char *url, const char *token,
                                   const char *json_body, int timeout_ms,
                                   feishu_http_response_t *out);
daima_err_t feishu_http_get(const char *url, const char *token,
                             int timeout_ms, feishu_http_response_t *out);
cJSON *feishu_http_parse_json(feishu_http_response_t *resp);
