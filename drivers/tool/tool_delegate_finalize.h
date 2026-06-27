#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "cjson.h"
#include "drivers/tool/tool_delegate_types.h"
#include "err.h"

err_t tool_delegate_finalize_sync_response(delegate_subagent_kind_t kind,
                                           const delegate_request_t *req,
                                           const char *session_id,
                                           cJSON *messages,
                                           const char *final_text,
                                           const char *reasoning_text,
                                           const char *raw_final_text,
                                           const char *raw_reasoning_text,
                                           bool tool_budget_exhausted,
                                           bool cancelled,
                                           char *output,
                                           size_t output_size);
