#pragma once

#include "err.h"
#include "cjson.h"

typedef struct {
    long status;
    char *body;
} feishu_http_response_t;

void feishu_http_response_free(feishu_http_response_t *resp);
err_t feishu_http_post_json(const char *url, const char *token,
                                   const char *json_body, int timeout_ms,
                                   feishu_http_response_t *out);
err_t feishu_http_get(const char *url, const char *token,
                             int timeout_ms, feishu_http_response_t *out);
cJSON *feishu_http_parse_json(feishu_http_response_t *resp);
