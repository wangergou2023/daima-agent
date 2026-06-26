/* Turn interview 短路接口。 */

#pragma once

#include "bus.h"
#include "err.h"

err_t agent_turn_try_interview(struct message *msg, char **out_final_text);
