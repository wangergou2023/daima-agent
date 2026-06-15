#pragma once

#include "err.h"

#include <stdbool.h>

enum intent {
    INTENT_QA = 0,          // 问答
    INTENT_IMPLEMENT,       // 实现
    INTENT_INVESTIGATE,     // 调研
    INTENT_FIX,             // 修复
    INTENT_OPEN,            // 开放
    INTENT_COUNT
};

typedef struct {
    bool enabled;
} intent_gate_cfg_t;

const char *intent_name(enum intent intent);
intent_gate_cfg_t intent_gate_load_cfg(void);
err_t intent_gate_classify(const char *user_message,
                                  enum intent *out_intent);
