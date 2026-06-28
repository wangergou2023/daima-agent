#pragma once

#include <stdbool.h>

#include "ipc/bus.h"

bool tool_orchestration_policy_requires_delegate_only(const struct message *msg);
