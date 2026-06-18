/* 技能工具列表——技能专属工具的注册与生命周期管理。
 * 每个技能可注册最多 SKILL_TOOLS_MAX 个自定义工具到 tool_bus。 */

#pragma once

#include "drivers/tool/tool_registry.h"
#include <stdbool.h>

#define SKILL_TOOLS_MAX 8  /* 每个技能最多注册的工具数 */

/* 技能工具包：一个技能的所有工具 */
typedef struct {
    char skill_name[64];         /* 所属技能名称 */
    struct tool tools[SKILL_TOOLS_MAX]; /* 工具数组 */
    int tool_count;              /* 已注册工具数 */
    bool active;                 /* 是否激活 */
} skill_tool_bundle_t;

/**
 * 从技能目录注册所有自定义工具到 tool_bus。
 * @param skill_name 技能名称
 * @param skill_dir  技能目录路径
 */
err_t skill_tools_register(const char *skill_name, const char *skill_dir);

/* 从 tool_bus 注销指定技能的所有工具 */
err_t skill_tools_unregister(const char *skill_name);

/* 注销所有已注册的技能工具 */
void skill_tools_unregister_all(void);
