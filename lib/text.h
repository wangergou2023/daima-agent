#pragma once

#include <stdbool.h>
#include <stddef.h>

void safe_copy(char *dst, size_t dst_size, const char *src);
bool str_ends_with(const char *s, const char *suffix);
void text_shorten(const char *src, char *dst, size_t dst_size, size_t max_len);
