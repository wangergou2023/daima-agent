/* Turn prompt 构建接口。 */

#pragma once

#include <stddef.h>

#include "bus.h"
#include "err.h"

err_t agent_turn_build_prompt(const struct message *msg,
			      char *system_prompt,
			      size_t system_prompt_size);
