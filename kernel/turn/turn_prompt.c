/* 单回合 prompt augment。 */
#include "turn_prompt.h"

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
