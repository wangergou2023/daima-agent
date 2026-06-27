#pragma once

#include <stdbool.h>
#include <stddef.h>

bool tool_delegate_text_has_dsml_markup(const char *text);
bool tool_delegate_text_has_transcript_markup_public(const char *text);
void tool_delegate_sanitize_summary_text_inplace(char *text);
void tool_delegate_sanitize_summary_text_copy(char *dst, size_t dst_size, const char *src);
bool tool_delegate_safe_text_is_directly_usable(const char *text);
