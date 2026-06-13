#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "core/bus.h"

int agent_env_int_or_default(const char *name, int fallback);
bool agent_env_bool_or_default(const char *name, bool fallback);

const char *agent_msg_source_or_default(const daima_msg_t *msg);
bool agent_msg_is_internal_control(const daima_msg_t *msg);
bool agent_msg_is_synthetic_event(const daima_msg_t *msg);
const char *agent_msg_role_for_current_turn(const daima_msg_t *msg);
const char *agent_session_role_for_inbound_msg(const daima_msg_t *msg);

void agent_chat_id_to_slug(const char *chat_id, char *buf, size_t size);
void agent_cleanup_inbound_msg(daima_msg_t *msg);
void agent_cleanup_outbound_msg(daima_msg_t *msg);
