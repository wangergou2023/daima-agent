#pragma once

#include <stddef.h>

char *base64_encode_alloc(const unsigned char *data,
                               size_t input_length,
                               size_t *output_length);
