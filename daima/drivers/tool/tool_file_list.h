/* 文件列目录辅助层。 */

#pragma once

#include <stddef.h>

#include "core/err.h"

daima_err_t tool_files_list_dir(const char *resolved_dir,
                               const char *prefix,
                               char *output,
                               size_t output_size,
                               int *count_out);
