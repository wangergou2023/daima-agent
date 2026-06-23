/* Turn prompt 构建接口。 */

#pragma once

#include <stddef.h>

#include "bus.h"
#include "err.h"
#include "plan.h"

err_t agent_turn_build_prompt(const struct message *msg,
			      const struct plan *plan,
			      char *system_prompt,
			      size_t system_prompt_size);
