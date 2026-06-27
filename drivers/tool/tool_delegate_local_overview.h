#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "drivers/tool/tool_delegate_types.h"

bool tool_delegate_try_local_repo_overview(const delegate_request_t *req,
                                           char *summary,
                                           size_t summary_size);
