/* 会话文件存储——通用文件读写辅助函数。 */

#include "drivers/memory/session_store_file_common.h"

#include <stdio.h>
#include <string.h>

/* 读取文件全部内容到缓冲区，自动添加 '\0' 结尾。 */
bool session_file_read_all(const char *path, char *buf, size_t buf_size, size_t *out_len)
{
    if (!path || !buf || buf_size == 0) {
        return false;
    }

    FILE *f = fopen(path, "r");
    if (!f) {
        return false;
    }

    size_t n = fread(buf, 1, buf_size - 1, f);
    fclose(f);
    buf[n] = '\0';
    if (out_len) {
        *out_len = n;
    }
    return true;
}

/* 将内容完整写入文件，覆盖已有内容。 */
bool session_file_write_all(const char *path, const char *content)
{
    if (!path || !content) {
        return false;
    }

    FILE *f = fopen(path, "w");
    if (!f) {
        return false;
    }

    size_t len = strlen(content);
    size_t written = fwrite(content, 1, len, f);
    fclose(f);
    return written == len;
}
