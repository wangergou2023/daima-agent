/* 扩展共享状态接口。
 * 提供跨 turn 的持久化状态存储，供扩展模块（extensions/）间共享数据。
 * 支持 plan 存取、角色配置、状态重置。状态在每次新会话时重置。 */

#pragma once

#include "roles.h"
#include "plan.h"

#include <stdbool.h>

/* 重置所有扩展共享状态（新会话开始时调用） */
void agent_extension_state_reset(void);

/* 获取当前会话的 plan（由 PLANNER 生成并存储于此） */
struct plan *agent_extension_state_plan(void);

/* 查询计划是否已经过评审 */
bool agent_extension_state_has_reviewed_plan(void);

/* 设置当前会话的角色配置（活跃角色 + 完整角色列表） */
void agent_extension_state_set_roles(const agent_role_t *roles, int role_count, agent_role_t active_role);

/* 获取当前会话的角色数量 */
int agent_extension_state_role_count(void);

/* 获取当前会话的活跃角色 */
agent_role_t agent_extension_state_active_role(void);
