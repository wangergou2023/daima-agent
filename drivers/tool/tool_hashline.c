/* 哈希行（Hashline）——安全编辑的前缀机制。
 * 每行标记 "行号#FNV1a哈希|"，用于在 apply_patch 时校验行内容未被漂移修改。
 * 哈希算法：FNV-1a 32-bit，取低 16 位以 4 位 hex 表示。 */

#include "drivers/tool/tool_hashline.h"

#include "hash.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* 计算单行内容的 4 位 hex 哈希值。 */
void hashline_hash_line(const char *content, char hash_out[5])
{
    if (!hash_out) {
        return;
    }
    uint32_t hash = fnv1a_32(content ? content : "", content ? strlen(content) : 0);
    snprintf(hash_out, 5, "%04x", (unsigned)(hash & 0xffffu));
}

/* 生成 "行号#hash|" 前缀字符串。 */
void hashline_make_prefix(int line_number, const char *line_content,
                          char *prefix_buf, size_t prefix_size)
{
    if (!prefix_buf || prefix_size == 0) {
        return;
    }
    char hash[5] = {0};
    hashline_hash_line(line_content, hash);
    snprintf(prefix_buf, prefix_size, "%d#%s|", line_number, hash);
}

/* 判断字符串是否为 4 位小写 hex（[0-9a-f]{4}）。 */
static bool is_lower_hex4(const char *text)
{
    if (!text) {
        return false;
    }
    for (int i = 0; i < 4; i++) {
        if (!((text[i] >= '0' && text[i] <= '9') || (text[i] >= 'a' && text[i] <= 'f'))) {
            return false;
        }
    }
    return true;
}

/* 剥离行首的 "行号#hash|" 前缀，返回原始内容指针。若格式不匹配则原样返回。 */
const char *hashline_strip_prefix(const char *line)
{
    if (!line || !isdigit((unsigned char)line[0])) {
        return line;
    }

    const char *p = line;
    while (isdigit((unsigned char)*p)) {
        p++;
    }
    if (*p != '#') {
        return line;
    }
    p++;
    if (!is_lower_hex4(p) || p[4] != '|') {
        return line;
    }
    return p + 5;
}

/**
 * 验证行内容与期望哈希值是否匹配。
 * @param line_number   行号
 * @param line_content  行内容（不含前缀）
 * @param expected_hash 期望的 4 位 hex 哈希
 * @return true 表示匹配
 */
bool hashline_verify_line(int line_number, const char *line_content,
                          const char *expected_hash)
{
    if (line_number <= 0 || !line_content || !expected_hash || !is_lower_hex4(expected_hash)) {
        return false;
    }

    char actual_hash[5] = {0};
    hashline_hash_line(line_content, actual_hash);
    return strncmp(actual_hash, expected_hash, 4) == 0;
}
