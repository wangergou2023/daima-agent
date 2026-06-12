#include "agent/agent_roles.h"

#include <stddef.h>

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
    [AGENT_ROLE_PLANNER] = {
        .name = "PLANNER",
        .category = "planning",
        .prompt_suffix = "你当前处于规划模式。请先分析用户需求，然后生成一个清晰的执行计划（步骤列表），不要直接编写代码。回复格式：## 执行计划\n1. ...\n2. ...",
    },
    [AGENT_ROLE_EXECUTOR] = {
        .name = "EXECUTOR",
        .category = "execution",
        .prompt_suffix = "你当前处于执行模式。请严格按照以下计划逐步执行，每完成一个步骤后确认完成状态，再继续下一步。不要跳跃或跳过步骤。",
    },
    [AGENT_ROLE_REVIEWER] = {
        .name = "REVIEWER",
        .category = "review",
        .prompt_suffix = "你当前处于审查模式。请检查前面的执行结果：1)每个步骤是否已完成 2)代码是否有遗漏或错误 3)是否需要补充或修正。如有问题请指出，如已完成请回复'审查通过'。",
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

const char *agent_role_category(agent_role_t role)
{
    const agent_role_def_t *def = role_def(role);
    return def ? def->category : "unknown";
}

int agent_roles_for_intent(daima_intent_t intent, agent_role_t roles_out[3])
{
    if (!roles_out) {
        return 0;
    }

    switch (intent) {
    case DAIMA_INTENT_QA:
    case DAIMA_INTENT_OPEN:
    case DAIMA_INTENT_INVESTIGATE:
        roles_out[0] = AGENT_ROLE_FAST;
        return 1;
    case DAIMA_INTENT_IMPLEMENT:
        roles_out[0] = AGENT_ROLE_PLANNER;
        roles_out[1] = AGENT_ROLE_EXECUTOR;
        roles_out[2] = AGENT_ROLE_REVIEWER;
        return 3;
    case DAIMA_INTENT_FIX:
        roles_out[0] = AGENT_ROLE_PLANNER;
        roles_out[1] = AGENT_ROLE_EXECUTOR;
        return 2;
    case DAIMA_INTENT_COUNT:
    default:
        return 0;
    }
}
