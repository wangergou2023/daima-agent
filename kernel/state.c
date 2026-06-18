/* 扩展共享状态存取：plan、roles、active_role 的全局状态单例。
 * 供扩展钩子（extensions/）在 turn 生命周期间传递和修改共享状态。 */

#include "state.h"

#include <string.h>

/* 全局单例状态 */
static struct plan s_plan;
static agent_role_t s_roles[3];
static int s_role_count;
static agent_role_t s_active_role = AGENT_ROLE_FAST;

/** 重置所有扩展共享状态为初始值。 */
void agent_extension_state_reset(void)
{
    memset(&s_plan, 0, sizeof(s_plan));
    memset(s_roles, 0, sizeof(s_roles));
    s_role_count = 0;
    s_active_role = AGENT_ROLE_FAST;
}

struct plan *agent_extension_state_plan(void)
{
    return &s_plan;
}

bool agent_extension_state_has_reviewed_plan(void)
{
    return s_plan.has_plan && s_plan.reviewed;
}

/** 设置角色列表和当前活跃角色（从 roles.c 的角色链结果）。 */
void agent_extension_state_set_roles(const agent_role_t *roles, int role_count, agent_role_t active_role)
{
    s_role_count = role_count;
    s_active_role = active_role;
    for (int i = 0; i < 3; i++) {
        s_roles[i] = (roles && i < role_count) ? roles[i] : AGENT_ROLE_FAST;
    }
}

int agent_extension_state_role_count(void)
{
    return s_role_count;
}

agent_role_t agent_extension_state_active_role(void)
{
    return s_active_role;
}
