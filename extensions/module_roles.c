/* Agent 角色模块：按意图分配角色链（FAST/PLANNER/EXECUTOR/REVIEWER），注入角色提示到 system prompt。 */

#include "hooks.h"
#include "roles.h"
#include "state.h"
#include "autoconf.h"
#include "linux/module.h"
#include "linux/printk.h"

#include <stdio.h>
#include <string.h>

/**
 * 根据计划评审状态选择活跃角色。
 * 无计划时用 roles[0]（FAST），已评审计划且有第二角色时用 roles[1]。
 * @param roles      角色数组（最多 3 个）
 * @param role_count 角色数量
 * @return 当前活跃角色
 */
static agent_role_t active_role_for_plan(const agent_role_t roles[3], int role_count)
{
    if (role_count <= 0) return AGENT_ROLE_FAST;
#if AGENT_EXTENSIONS_ENABLED
    if (agent_extension_state_has_reviewed_plan() && role_count > 1) return roles[1];
#endif
    return roles[0];
}

/**
 * intent 钩子：按意图解析角色链，设置活跃角色。
 * @param msg 入站消息
 * @return 始终返回 0
 */
static err_t on_intent(struct message *msg)
{
#if AGENT_EXTENSIONS_ENABLED
    agent_role_t roles[3] = {0};
    int role_count = agent_roles_for_intent(msg->intent, roles);
    agent_role_t active_role = active_role_for_plan(roles, role_count);
    agent_extension_state_set_roles(roles, role_count, active_role);
    if (role_count > 0) {
        pr_info("Agent roles for intent=%s: %s (chain of %d)", intent_name(msg->intent), agent_role_name(roles[0]), role_count);
    }
#endif
    return 0;
}

/**
 * prepare 钩子：将当前角色名和角色提示追加到 system prompt 末尾。
 * @param msg               入站消息（未使用）
 * @param system_prompt     system prompt 缓冲区
 * @param system_prompt_size 缓冲区大小
 * @param messages          JSON 消息数组（未使用）
 * @return 始终返回 0
 */
static err_t on_prepare(struct message *msg, char *system_prompt,
                              size_t system_prompt_size, cJSON *messages)
{
    (void)msg;
    (void)messages;
#if AGENT_EXTENSIONS_ENABLED
    if (!system_prompt || system_prompt_size == 0 || agent_extension_state_role_count() <= 0) return 0;
    agent_role_t role = agent_extension_state_active_role();
    size_t off = strnlen(system_prompt, system_prompt_size - 1);
    if (off >= system_prompt_size - 1) return 0;
    int written = snprintf(system_prompt + off, system_prompt_size - off,
                           "\n\n## 当前角色: %s\n%s\n",
                           agent_role_name(role), agent_role_prompt_suffix(role));
    if (written < 0 || (size_t)written >= system_prompt_size - off) {
        system_prompt[system_prompt_size - 1] = '\0';
    }
#endif
    return 0;
}

static agent_extension_hooks_t ext = {
    .name = "agent_roles",
    .on_intent = on_intent,
    .on_prepare = on_prepare,
    .enabled = true,
};

int __init roles_module_init(void)
{
    agent_hooks_register(&ext);
    return 0;
}

module_init(roles_module_init);
