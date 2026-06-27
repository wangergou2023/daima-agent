#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "drivers/tool/tool_delegate_path_resolve.h"
#include "drivers/tool/tool_delegate_types.h"

bool tool_delegate_request_is_bounded_explore_overview(const delegate_request_t *req);
bool tool_delegate_overview_request_preserves_repo_root(const char *prompt, const char *description);
bool tool_delegate_should_expand_repo_root_overview_batch(const delegate_request_t *req);
void tool_delegate_fill_repo_root_overview_batch_request(const delegate_request_t *req,
                                                         delegate_request_t *batch_req);
