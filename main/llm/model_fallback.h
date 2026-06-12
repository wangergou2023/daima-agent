#pragma once

#include "daima_err.h"
#include "llm/llm_proxy.h"
#include "cJSON.h"

#include <stdbool.h>

#define FALLBACK_MAX_MODELS 5

typedef struct {
    bool enabled;
    char models[FALLBACK_MAX_MODELS][64];
    int model_count;
} model_fallback_cfg_t;

model_fallback_cfg_t model_fallback_load_cfg(void);
daima_err_t model_fallback_chat_with_fallback(
    const char *system_prompt,
    cJSON *messages,
    const char *tools_json,
    llm_response_t *resp);
