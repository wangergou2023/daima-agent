#pragma once

#include <stdbool.h>
#include <stddef.h>

void tool_delegate_build_safe_output_text(const char *final_text,
                                          const char *reasoning_text,
                                          bool tool_budget_exhausted,
                                          bool cancelled,
                                          char *summary,
                                          size_t summary_size);
bool tool_delegate_try_fast_local_json(const char *subagent_type,
                                       const char *description,
                                       const char *raw_text,
                                       char *summary,
                                       size_t summary_size);
