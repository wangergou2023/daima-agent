/* 工具基础类型定义。 */

#pragma once

#include "err.h"
#include <stddef.h>
#include "linux/driver.h"

struct tool {
    const char *name;
    const char *description;
    const char *input_schema_json;
    err_t (*execute)(const char *input_json, char *output, size_t output_size);
};

struct tool_device {
    const char *name;
    const char *description;
    const char *input_schema_json;
};

struct tool_driver {
    struct driver drv;
    err_t (*execute)(const char *input_json, char *output, size_t output_size);
};
