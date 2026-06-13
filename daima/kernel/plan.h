#pragma once

#include "intent.h"
#include "err.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char plan_text[4096];
    bool has_plan;
    bool reviewed;
} daima_plan_t;

daima_err_t plan_review_generate(daima_intent_t intent,
                                  const char *user_message,
                                  const char *system_prompt,
                                  daima_plan_t *out_plan);

daima_err_t plan_review_inject_to_prompt(const daima_plan_t *plan,
                                          char *system_prompt,
                                          size_t system_prompt_size);
