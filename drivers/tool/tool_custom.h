/* 自定义工具：从 JSON 零编译加载 */
#pragma once
#include "err.h"
int tool_custom_load(const char *path);
int tool_custom_load_default(void);
err_t tool_custom_execute(const char *name, const char *input, char *output, size_t size);