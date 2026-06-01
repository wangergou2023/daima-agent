#include "daima_text.h"

#include <string.h>

void daima_safe_copy(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t n = strnlen(src, dst_size - 1);
    memcpy(dst, src, n);
    dst[n] = '\0';
}

bool daima_str_ends_with(const char *s, const char *suffix)
{
    if (!s || !suffix) return false;
    size_t s_len = strlen(s);
    size_t suf_len = strlen(suffix);
    if (s_len < suf_len) return false;
    return strcmp(s + s_len - suf_len, suffix) == 0;
}
