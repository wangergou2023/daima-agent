#pragma once

#include <stdbool.h>
#include <stddef.h>

bool delegate_turn_directive_store(const char *chat_id, const char *directive_json);
bool delegate_turn_directive_load_copy(const char *chat_id, char *buf, size_t buf_size);
void delegate_turn_directive_clear(const char *chat_id);
