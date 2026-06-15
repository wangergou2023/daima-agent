#pragma once

#include <stddef.h>

#include "bus.h"

void agent_channel_policy_append(char *prompt, size_t size, const struct message *msg);
