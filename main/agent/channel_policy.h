#pragma once

#include <stddef.h>

#include "bus/message_bus.h"

void agent_channel_policy_append(char *prompt, size_t size, const daima_msg_t *msg);
