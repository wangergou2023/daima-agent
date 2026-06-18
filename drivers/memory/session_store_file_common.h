/* 会话文件存储——通用文件读写接口。 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "err.h"

/* 读取文件全部内容 */
bool session_file_read_all(const char *path, char *buf, size_t buf_size, size_t *out_len);
/* 写入文件全部内容 */
bool session_file_write_all(const char *path, const char *content);
