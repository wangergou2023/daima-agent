#pragma once

#include <stdbool.h>

#include "drivers/llm/llm_proxy.h"

cJSON *llm_anthropic_build_tools_body(const char *system_prompt,
                                      cJSON *messages,
                                      const char *tools_json,
                                      const char *model,
                                      int max_tokens,
                                      bool disable_thinking,
                                      const char *reasoning_effort);

#ifdef ENABLE_VISION
cJSON *llm_anthropic_build_image_body(const char *system_prompt,
                                      const char *user_text,
                                      const llm_image_content_t *images,
                                      int image_count,
                                      const char *model,
                                      int max_tokens,
                                      bool disable_thinking,
                                      const char *reasoning_effort);
#endif

err_t llm_anthropic_parse_response(const char *json_text, llm_response_t *resp);
