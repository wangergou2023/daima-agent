/* Turn reply 处理接口。 */

#pragma once

#include <stdbool.h>

#include "bus.h"
#include "err.h"

void agent_turn_handle_reply(struct message *msg,
			     char **io_final_text,
			     char **io_reasoning_text,
			     err_t turn_err,
			     int iteration,
			     bool tool_budget_exhausted,
			     bool cancelled);
