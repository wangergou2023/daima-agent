#pragma once

#include <stdbool.h>

#include "drivers/llm/llm_proxy.h"

cJSON *llm_openai_build_tools_body(const char *system_prompt,
                                   cJSON *messages,
                                   const char *tools_json,
                                   const char *model,
                                   int max_completion_tokens,
                                   bool use_max_tokens_field,
                                   bool disable_thinking,
                                   const char *reasoning_effort,
                                   bool add_reasoning_content);

#ifdef DAIMA_ENABLE_VISION
cJSON *llm_openai_build_image_body(const char *system_prompt,
                                   const char *user_text,
                                   const llm_image_content_t *images,
                                   int image_count,
                                   const char *model,
                                   int max_completion_tokens,
                                   bool use_max_tokens_field,
                                   bool disable_thinking,
                                   const char *reasoning_effort);
#endif

daima_err_t llm_openai_parse_response(const char *json_text, llm_response_t *resp);
