#pragma once

#include "drivers/tool/tool_delegate_local_overview.h"
#include "drivers/tool/tool_delegate_path_resolve.h"
#include <stdbool.h>
#include <stddef.h>

#include "drivers/tool/tool_delegate_types.h"

bool tool_delegate_prepare_subagent_prompt(const char *subagent_type,
                                           const char *description,
                                           const char *prompt,
                                           char *prepared_prompt,
                                           size_t prepared_prompt_size,
                                           bool *disable_tools);
bool tool_delegate_request_is_bounded_explore_overview(const delegate_request_t *req);
bool tool_delegate_overview_request_preserves_repo_root(const char *prompt, const char *description);
