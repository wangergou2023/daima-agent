/* Base64 编码接口。 */

#pragma once

#include <stddef.h>

/** Base64 编码，动态分配输出（调用方 kfree 释放）。@param output_length 可为 NULL */
char *base64_encode_alloc(const unsigned char *data,
                               size_t input_length,
                               size_t *output_length);
