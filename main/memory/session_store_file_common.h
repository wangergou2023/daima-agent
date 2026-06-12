#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "daima_err.h"

bool session_file_read_all(const char *path, char *buf, size_t buf_size, size_t *out_len);
bool session_file_write_all(const char *path, const char *content);
