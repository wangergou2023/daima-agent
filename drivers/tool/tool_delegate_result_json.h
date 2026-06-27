#pragma once

#include <stdbool.h>
#include <stddef.h>

bool tool_delegate_parse_result_json_summary(const char *text, char *summary, size_t summary_size);
bool tool_delegate_parse_result_json_rendered(const char *text, char *summary, size_t summary_size);
bool tool_delegate_result_json_has_nonempty_evidence(const char *text);
bool tool_delegate_extract_sync_final_output(const char *text,
                                             char *summary,
                                             size_t summary_size);
