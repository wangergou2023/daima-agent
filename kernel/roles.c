/* Agent 角色定义与映射。 */

#include "roles.h"

#include <stddef.h>

/* 角色定义结构体：名称、类别、prompt 后缀指令 */
typedef struct {
    const char *name;
    const char *category;
    const char *prompt_suffix;
} agent_role_def_t;

static const agent_role_def_t s_role_defs[AGENT_ROLE_COUNT] = {
    [AGENT_ROLE_FAST] = {
        .name = "FAST",
        .category = "fast",
        .prompt_suffix = "简洁直接地回答用户的问题，不需要过度解释。",
    },
    [AGENT_ROLE_ORACLE] = {
        .name = "ORACLE",
        .category = "deep",
        .prompt_suffix = "你当前处于架构与判断模式。请给出基于证据的分析、风险和建议，不要拍脑袋。",
    },
    [AGENT_ROLE_IMPLEMENT] = {
        .name = "IMPLEMENT",
        .category = "deep",
        .prompt_suffix = "你当前处于实现模式。请直接完成用户请求，必要时调用工具，不要额外制造多角色流程。",
    },
};

static const agent_role_def_t *role_def(agent_role_t role)
{
    if (role < 0 || role >= AGENT_ROLE_COUNT) {
        return NULL;
    }
    return &s_role_defs[role];
}

const char *agent_role_name(agent_role_t role)
{
    const agent_role_def_t *def = role_def(role);
    return def ? def->name : "UNKNOWN";
}

const char *agent_role_prompt_suffix(agent_role_t role)
{
    const agent_role_def_t *def = role_def(role);
    return def ? def->prompt_suffix : "";
}

