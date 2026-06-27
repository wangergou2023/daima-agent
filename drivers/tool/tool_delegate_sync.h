#pragma once

#include "drivers/tool/tool_delegate_types.h"

err_t tool_delegate_run_sync_single_subagent(delegate_subagent_kind_t kind,
                                             const delegate_request_t *req,
                                             const char *task_id,
                                             const char *session_id,
                                             const char *coordinator_id,
                                             const char *parent_chat_id,
                                             char *output,
                                             size_t output_size);
