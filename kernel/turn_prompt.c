/* 单回合 prompt augment。 */
#include "turn_prompt.h"

#include "team.h"
#include <stdio.h>
#include <string.h>

void agent_turn_append_role_prompt(char *system_prompt,
				      size_t system_prompt_size,
				      agent_role_t role)
{
	if (!system_prompt || system_prompt_size == 0) {
		return;
	}

	size_t off = strnlen(system_prompt, system_prompt_size - 1);
	if (off >= system_prompt_size - 1) {
		return;
	}

	int written = snprintf(system_prompt + off, system_prompt_size - off,
			       "\n\n## 当前角色: %s\n%s\n",
			       agent_role_name(role), agent_role_prompt_suffix(role));
	if (written < 0 || (size_t)written >= system_prompt_size - off) {
		system_prompt[system_prompt_size - 1] = '\0';
	}
}

void agent_turn_inject_team_guidance(char *system_prompt,
				       size_t system_prompt_size,
				       const struct plan *plan,
				       const char *tools_json)
{
	if (!system_prompt || system_prompt_size == 0 || !plan ||
	    !plan->has_plan || !plan->reviewed) {
		return;
	}

	team_orchestrator_t team = {0};
	err_t err = team_mode_orchestrate(plan, system_prompt, tools_json, &team);
	if (err != 0 || team.completed_count <= 0) {
		return;
	}

	(void)team_mode_inject_to_prompt(&team, system_prompt, system_prompt_size);
}
