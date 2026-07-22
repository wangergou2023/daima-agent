/* Turn reply 处理接口。 */

#pragma once

#include <stdbool.h>

#include "bus.h"
#include "err.h"
#include "turn_exec.h"
#include "turn_decision.h"

void agent_turn_handle_reply(struct message *msg,
			     char **io_final_text,
			     char **io_reasoning_text,
			     err_t turn_err,
			     int iteration,
			     bool tool_budget_exhausted,
			     bool cancelled,
			     const turn_exec_stats_t *stats,
			     const agent_turn_decision_t *decision);
