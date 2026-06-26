/* 文本工具：安全复制、后缀判断、截断。 */

#include "text.h"

#include <string.h>
#include "linux/kernel.h"
#include "context_build.h"

static size_t utf8_seq_len(unsigned char c)
{
    if ((c & 0x80) == 0x00) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 0;
}

/**
 * 安全字符串复制，使用 strnlen 防止溢出，确保 null 终止。
 * @param dst      目标缓冲区
 * @param dst_size 缓冲区大小
 * @param src      源字符串（可为 NULL）
 */
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

/**
 * 判断字符串是否以指定后缀结尾。
 * @param s      源字符串
 * @param suffix 后缀
 * @return 是返回 true
 */
bool str_ends_with(const char *s, const char *suffix)
{
    if (!s || !suffix) return false;
    size_t s_len = strlen(s);
    size_t suf_len = strlen(suffix);
    if (s_len < suf_len) return false;
    return strcmp(s + s_len - suf_len, suffix) == 0;
}

/**
 * 截断字符串，超长时尾部追加 "..."。
 * @param src      源字符串
 * @param dst      输出缓冲区
 * @param dst_size 缓冲区大小
 * @param max_len  最大保留长度（不含 "..."）
 */
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

void text_sanitize_utf8_json(char *s)
{
    if (!s || !s[0]) return;

    context_fix_truncated_utf8(s, strlen(s));

    unsigned char *src = (unsigned char *)s;
    unsigned char *dst = (unsigned char *)s;

    while (*src) {
        size_t seq_len = utf8_seq_len(*src);
        if (seq_len == 0) {
            src++;
            continue;
        }

        if (seq_len == 1) {
            if (*src >= 0x20 || *src == '\n' || *src == '\r' || *src == '\t') {
                *dst++ = *src;
            }
            src++;
            continue;
        }

        bool valid = true;
        for (size_t i = 1; i < seq_len; i++) {
            unsigned char ch = src[i];
            if (ch == '\0' || (ch & 0xC0) != 0x80) {
                valid = false;
                break;
            }
        }
        if (!valid) {
            src++;
            continue;
        }

        unsigned int codepoint = 0;
        if (seq_len == 2) {
            codepoint = ((src[0] & 0x1F) << 6) |
                        (src[1] & 0x3F);
            if (codepoint < 0x80) valid = false;
        } else if (seq_len == 3) {
            codepoint = ((src[0] & 0x0F) << 12) |
                        ((src[1] & 0x3F) << 6) |
                        (src[2] & 0x3F);
            if (codepoint < 0x800) valid = false;
        } else {
            codepoint = ((src[0] & 0x07) << 18) |
                        ((src[1] & 0x3F) << 12) |
                        ((src[2] & 0x3F) << 6) |
                        (src[3] & 0x3F);
            if (codepoint < 0x10000 || codepoint > 0x10FFFF) valid = false;
        }

        if (codepoint >= 0xD800 && codepoint <= 0xDFFF) {
            valid = false;
        }

        if (!valid) {
            src += seq_len;
            continue;
        }

        for (size_t i = 0; i < seq_len; i++) {
            *dst++ = *src++;
        }
    }

    *dst = '\0';
}
