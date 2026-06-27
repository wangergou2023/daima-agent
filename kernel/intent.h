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

const char *intent_name(enum intent intent);
err_t intent_gate_classify(const char *user_message,
                                  enum intent *out_intent);
bool intent_gate_text_looks_like_action_request_for_test(const char *user_message);
enum intent intent_gate_fallback_for_text(const char *user_message);
