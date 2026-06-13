#pragma once

#include <stddef.h>

char *daima_base64_encode_alloc(const unsigned char *data,
                               size_t input_length,
                               size_t *output_length);
