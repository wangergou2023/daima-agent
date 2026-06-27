/* delegate_task preflight execution helpers */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "cjson.h"
#include "drivers/tool/tool_delegate_types.h"
#include "err.h"

struct message;

err_t tool_delegate_execute_preflight_tool(const delegate_request_t *req,
                                           const struct message *msg,
                                           cJSON *messages,
                                           const char *task_id,
                                           const char *session_id,
                                           const char *coordinator_id,
                                           const char *parent_chat_id,
                                           const char *subagent_type,
                                           const char *description,
                                           const char *scope_path,
                                           const char *scope_kind,
                                           const char *analysis_focus,
                                           char *final_summary,
                                           size_t final_summary_size,
                                           bool *out_blocked);
