#include "rules.h"

#include <stdio.h>
#include <string.h>

#define RULES_FILE_LIMIT 4096

static size_t bounded_len(const char *s, size_t max)
{
    size_t len = 0;
    if (!s) {
        return 0;
    }
    while (len < max && s[len]) {
        len++;
    }
    return len;
}

static void append_bytes(char *buffer, size_t buffer_size, size_t *offset, const char *data, size_t data_len)
{
    if (!buffer || buffer_size == 0 || !offset || !data || data_len == 0) {
        return;
    }
    if (*offset >= buffer_size - 1) {
        buffer[buffer_size - 1] = '\0';
        return;
    }

    size_t available = buffer_size - 1 - *offset;
    size_t copy_len = data_len < available ? data_len : available;
    memcpy(buffer + *offset, data, copy_len);
    *offset += copy_len;
    buffer[*offset] = '\0';
}

static bool append_file_limited(char *buffer, size_t buffer_size, size_t *offset, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        return false;
    }

    char file_buf[RULES_FILE_LIMIT];
    size_t n = fread(file_buf, 1, sizeof(file_buf), f);
    fclose(f);
    if (n == 0) {
        return false;
    }

    append_bytes(buffer, buffer_size, offset, file_buf, n);
    if (file_buf[n - 1] != '\n') {
        append_bytes(buffer, buffer_size, offset, "\n", 1);
    }
    return true;
}

err_t rules_injection_load(char *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size == 0) {
        return ERR_INVALID_ARG;
    }

    buffer[0] = '\0';
    size_t offset = 0;
    const char *header = "## 项目规则\n\n";
    append_bytes(buffer, buffer_size, &offset, header, bounded_len(header, 64));

    const char *paths[] = {
        "AGENTS.md",
        "spiffs_data/config/AGENTS.md",
    };
    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        size_t before = offset;
        if (append_file_limited(buffer, buffer_size, &offset, paths[i]) && before != offset) {
            append_bytes(buffer, buffer_size, &offset, "\n", 1);
        }
    }

    return 0;
}
