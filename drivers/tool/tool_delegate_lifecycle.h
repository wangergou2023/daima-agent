#pragma once

#include <stdbool.h>

#include "err.h"

err_t delegate_launch_ready_background_subagents_for_runtime(void);
err_t delegate_lifecycle_poll_runtime(void);
bool delegate_lifecycle_runtime_is_idle(void);
