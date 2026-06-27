#pragma once

#include <stddef.h>

#include "drivers/tool/tool_delegate_types.h"
#include "err.h"

err_t tool_delegate_run_background_coordinator(const delegate_request_t *req,
                                               const char *parent_chat_id,
                                               char *output,
                                               size_t output_size);
err_t tool_delegate_run_background_subagent(delegate_subagent_kind_t kind,
                                            const delegate_request_t *req,
                                            const char *coordinator_id,
                                            const char *parent_chat_id,
                                            char *output,
                                            size_t output_size);
