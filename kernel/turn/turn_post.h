/* Turn 收尾副作用接口。 */

#pragma once

#include <stdbool.h>

#include "bus.h"
#include "err.h"

void agent_turn_run_post_actions(struct message *msg,
				 err_t turn_err,
				 bool cancelled);
