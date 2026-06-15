#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "bus.h"

int agent_env_int_or_default(const char *name, int fallback);
bool agent_env_bool_or_default(const char *name, bool fallback);

const char *agent_msg_source_or_default(const struct message *msg);
bool agent_msg_is_internal_control(const struct message *msg);
bool agent_msg_is_synthetic_event(const struct message *msg);
const char *agent_msg_role_for_current_turn(const struct message *msg);
const char *agent_session_role_for_inbound_msg(const struct message *msg);

void agent_chat_id_to_slug(const char *chat_id, char *buf, size_t size);
void agent_cleanup_inbound_msg(struct message *msg);
void agent_cleanup_outbound_msg(struct message *msg);
