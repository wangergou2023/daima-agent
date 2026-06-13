#include "drivers/tool/tool_hashline.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static uint32_t hashline_fnv1a_32(const char *data, size_t len)
{
    uint32_t hash = 0x811c9dc5;
    for (size_t i = 0; i < len; i++) {
        hash ^= (uint8_t)data[i];
        hash *= 0x01000193;
    }
    return hash;
}

void hashline_hash_line(const char *content, char hash_out[5])
{
    if (!hash_out) {
        return;
    }
    uint32_t hash = hashline_fnv1a_32(content ? content : "", content ? strlen(content) : 0);
    snprintf(hash_out, 5, "%04x", (unsigned)(hash & 0xffffu));
}

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
