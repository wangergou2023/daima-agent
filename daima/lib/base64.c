#include "base64.h"

#include <stdint.h>
#include <stdlib.h>
#include "linux/slab.h"

static const char s_b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

char *base64_encode_alloc(const unsigned char *data,
                               size_t input_length,
                               size_t *output_length)
{
    if (!data || input_length == 0) return NULL;

    size_t out_len = 4 * ((input_length + 2) / 3);
    if (output_length) {
        *output_length = out_len;
    }

    char *encoded = kmalloc(out_len + 1, GFP_KERNEL);
    if (!encoded) return NULL;

    for (size_t i = 0, j = 0; i < input_length;) {
        uint32_t octet_a = i < input_length ? data[i++] : 0;
        uint32_t octet_b = i < input_length ? data[i++] : 0;
        uint32_t octet_c = i < input_length ? data[i++] : 0;
        uint32_t triple = (octet_a << 16) + (octet_b << 8) + octet_c;

        encoded[j++] = s_b64_table[(triple >> 18) & 0x3F];
        encoded[j++] = s_b64_table[(triple >> 12) & 0x3F];
        encoded[j++] = s_b64_table[(triple >> 6) & 0x3F];
        encoded[j++] = s_b64_table[triple & 0x3F];
    }

    for (size_t i = 0; i < (3 - input_length % 3) % 3; i++) {
        encoded[out_len - 1 - i] = '=';
    }
    encoded[out_len] = '\0';
    return encoded;
}
