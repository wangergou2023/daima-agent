/* FNV-1a 32-bit 哈希工具。
 * 用于 hashline 行指纹和安全编辑指纹校验。 */

#pragma once

#include <stddef.h>
#include <stdint.h>

/** FNV-1a 32-bit 哈希（初始种子 0x811c9dc5，质数 0x01000193）。 */
static inline uint32_t fnv1a_32(const char *data, size_t len)
{
    uint32_t hash = 0x811c9dc5;
    for (size_t i = 0; i < len; i++) {
        hash ^= (uint8_t)data[i];
        hash *= 0x01000193;
    }
    return hash;
}
