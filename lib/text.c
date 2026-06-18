/* 文本工具：安全复制、后缀判断、截断。 */

#include "text.h"

#include <string.h>
#include "linux/kernel.h"

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
