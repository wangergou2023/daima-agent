/* delegate_task response/session helpers */
#pragma once

#include <stddef.h>

#include "err.h"

err_t tool_delegate_write_json_response(char *output,
                                        size_t output_size,
                                        const char *task_id,
                                        const char *session_id,
                                        const char *status,
                                        const char *delivery,
                                        const char *subagent_type,
                                        const char *description,
                                        const char *model,
                                        const char *payload_output);
void tool_delegate_persist_turn_session(const char *session_id,
                                        const char *user_prompt,
                                        const char *assistant_text,
                                        const char *reasoning_text);
const char *tool_delegate_visible_output_or_fallback(const char *raw_output,
                                                     char *visible_output,
                                                     size_t visible_output_size);
