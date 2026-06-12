#pragma once

#include <stdbool.h>
#include <stddef.h>

void daima_safe_copy(char *dst, size_t dst_size, const char *src);
bool daima_str_ends_with(const char *s, const char *suffix);
void daima_shorten_text(const char *src, char *dst, size_t dst_size, size_t max_len);
