#pragma once

#include "tools/tool_registry.h"
#include <stdbool.h>

#define SKILL_TOOLS_MAX 8

typedef struct {
    char skill_name[64];
    daima_tool_t tools[SKILL_TOOLS_MAX];
    int tool_count;
    bool active;
} skill_tool_bundle_t;

daima_err_t skill_tools_register(const char *skill_name, const char *skill_dir);
daima_err_t skill_tools_unregister(const char *skill_name);
void skill_tools_unregister_all(void);
