/* 文本工具接口：安全复制、后缀判断、截断。 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

/** 安全字符串复制，确保 null 终止。 */
void safe_copy(char *dst, size_t dst_size, const char *src);

/** 判断字符串是否以指定后缀结尾。 */
bool str_ends_with(const char *s, const char *suffix);

/** 截断字符串，超长时追加 "..."。 */
void text_shorten(const char *src, char *dst, size_t dst_size, size_t max_len);

/** 原地清洗字符串中的非法 UTF-8 / Unicode 码点，保证可安全序列化为 JSON。 */
void text_sanitize_utf8_json(char *s);
