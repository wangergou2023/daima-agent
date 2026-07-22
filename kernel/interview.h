#pragma once

#include "err.h"
#include <stdbool.h>

typedef struct {
    bool enabled;
    bool needs_interview;  // 当前消息是否需要访谈
    char questions[2048];  // 生成的提问
} prometheus_state_t;

err_t prometheus_check_needs_interview(const char *user_message,
                                             prometheus_state_t *out);
