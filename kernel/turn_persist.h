#pragma once

#include <stdbool.h>

#include "bus.h"

void agent_turn_save_session(const struct message *msg, const char *final_text, const char *reasoning, int iteration);
void agent_turn_queue_outbound_text(const struct message *msg, char *text, const char *reasoning, bool free_on_fail);
char *agent_turn_build_error_reply(bool tool_budget_exhausted);
