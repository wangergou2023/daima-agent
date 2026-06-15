#pragma once

#include "err.h"
#include "drivers/llm/llm_proxy.h"
#include "cjson.h"

#include <stdbool.h>

#define FALLBACK_MAX_MODELS 5

typedef struct {
    bool enabled;
    char models[FALLBACK_MAX_MODELS][64];
    int model_count;
} model_fallback_cfg_t;

model_fallback_cfg_t model_fallback_load_cfg(void);
err_t model_fallback_chat_with_fallback(
    const char *system_prompt,
    cJSON *messages,
    const char *tools_json,
    llm_response_t *resp);
