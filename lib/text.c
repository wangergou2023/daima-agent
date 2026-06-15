#include "text.h"

#include <string.h>
#include "linux/kernel.h"

void safe_copy(char *dst, size_t dst_size, const char *src)
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

bool str_ends_with(const char *s, const char *suffix)
{
    if (!s || !suffix) return false;
    size_t s_len = strlen(s);
    size_t suf_len = strlen(suffix);
    if (s_len < suf_len) return false;
    return strcmp(s + s_len - suf_len, suffix) == 0;
}

void text_shorten(const char *src, char *dst, size_t dst_size, size_t max_len)
{
    if (!dst || dst_size == 0) return;
    dst[0] = '\0';
    if (!src || !src[0]) return;

    size_t len = strlen(src);
    if (len <= max_len || max_len + 4 >= dst_size) {
        strscpy(dst, src, dst_size);
        return;
    }

    snprintf(dst, dst_size, "%.*s...", (int)max_len, src);
}
