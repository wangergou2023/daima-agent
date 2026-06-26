/* 单回合 prompt augment：角色提示。 */

#pragma once

#include <stddef.h>

#include "roles.h"

void agent_turn_append_role_prompt(char *system_prompt,
				      size_t system_prompt_size,
				      agent_role_t role);
