/* 回合状态接口。
 * 保存默认主链在单次 turn 内生成的 plan、角色链和活跃角色。 */

#pragma once

#include "roles.h"
#include "plan.h"

#include <stdbool.h>

/* 重置当前 turn 的主链状态。 */
void agent_turn_state_reset(void);

/* 获取当前 turn 的 plan。 */
struct plan *agent_turn_state_plan(void);

/* 查询当前 turn 是否存在已评审 plan。 */
bool agent_turn_state_has_reviewed_plan(void);

/* 设置当前 turn 的角色配置。 */
void agent_turn_state_set_roles(const agent_role_t *roles, int role_count, agent_role_t active_role);

/* 获取当前 turn 的角色数量。 */
int agent_turn_state_role_count(void);

/* 获取当前 turn 的活跃角色。 */
agent_role_t agent_turn_state_active_role(void);
