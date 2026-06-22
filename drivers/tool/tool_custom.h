/* 自定义工具：从 JSON 零编译加载 */
#pragma once
#include "err.h"

struct tool_custom_meta {
    const char *name;
    const char *description;
    const char *input_schema_json;
};

int tool_custom_load(const char *path);
int tool_custom_load_default(void);
err_t tool_custom_execute(const char *name, const char *input, char *output, size_t size);
int tool_custom_count(void);
const struct tool_custom_meta *tool_custom_get(int index);
