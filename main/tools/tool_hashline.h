#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HASHLINE_PREFIX_MAX 16

void hashline_make_prefix(int line_number, const char *line_content,
                          char *prefix_buf, size_t prefix_size);
const char *hashline_strip_prefix(const char *line);
bool hashline_verify_line(int line_number, const char *line_content,
                          const char *expected_hash);
void hashline_hash_line(const char *content, char hash_out[5]);
