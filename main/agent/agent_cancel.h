#pragma once

#include <stdbool.h>
#include <stdint.h>

uint64_t agent_cancel_begin_turn(const char *chat_id);
void agent_cancel_request(const char *chat_id, const char *reason);
bool agent_cancel_is_cancelled(const char *chat_id, uint64_t token);

void agent_cancel_enter_current_turn(const char *chat_id, uint64_t token);
void agent_cancel_leave_current_turn(void);
bool agent_cancel_current_thread_cancelled(void);
