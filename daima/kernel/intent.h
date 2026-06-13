#pragma once

#include "err.h"

#include <stdbool.h>

typedef enum {
    DAIMA_INTENT_QA = 0,          // 问答
    DAIMA_INTENT_IMPLEMENT,       // 实现
    DAIMA_INTENT_INVESTIGATE,     // 调研
    DAIMA_INTENT_FIX,             // 修复
    DAIMA_INTENT_OPEN,            // 开放
    DAIMA_INTENT_COUNT
} daima_intent_t;

typedef struct {
    bool enabled;
} intent_gate_cfg_t;

const char *daima_intent_name(daima_intent_t intent);
intent_gate_cfg_t intent_gate_load_cfg(void);
daima_err_t intent_gate_classify(const char *user_message,
                                  daima_intent_t *out_intent);
