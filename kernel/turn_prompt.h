/* 单回合 prompt augment：角色提示与 team guidance。 */

#pragma once

#include <stddef.h>

#include "plan.h"
#include "roles.h"

void agent_turn_append_role_prompt(char *system_prompt,
				      size_t system_prompt_size,
				      agent_role_t role);
void agent_turn_inject_team_guidance(char *system_prompt,
				       size_t system_prompt_size,
				       const struct plan *plan,
				       const char *tools_json);
