#pragma once

#include "intent.h"
#include "err.h"

#include <stdbool.h>
#include <stddef.h>

struct plan {
    char plan_text[4096];
    bool has_plan;
    bool reviewed;
};

err_t plan_review_generate(enum intent intent,
                                  const char *user_message,
                                  const char *system_prompt,
                                  struct plan *out_plan);

err_t plan_review_inject_to_prompt(const struct plan *plan,
                                           char *system_prompt,
                                           size_t system_prompt_size);
