#pragma once

#include <stdbool.h>

#include "bus/message_bus.h"

void agent_turn_save_session(const daima_msg_t *msg, const char *final_text, int iteration);
void agent_turn_queue_outbound_text(const daima_msg_t *msg, char *text, bool free_on_fail);
char *agent_turn_build_error_reply(bool tool_budget_exhausted);
