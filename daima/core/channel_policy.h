#pragma once

#include <stddef.h>

#include "core/bus.h"

void agent_channel_policy_append(char *prompt, size_t size, const daima_msg_t *msg);
