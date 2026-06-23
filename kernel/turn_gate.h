/* 单回合入口前置处理。 */

#pragma once

#include <stdbool.h>

#include "bus.h"
#include "err.h"

bool agent_turn_handle_self_test_command(struct message *msg);
err_t agent_turn_validate_inbound_message(struct message *msg);
