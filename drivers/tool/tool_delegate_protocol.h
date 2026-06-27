#pragma once

#include "drivers/tool/tool_delegate_result_json.h"
#include "drivers/tool/tool_delegate_safe_output.h"
#include "drivers/tool/tool_delegate_sanitize.h"
#include "drivers/tool/tool_delegate_types.h"

bool tool_delegate_finalize_result_json(const char *subagent_type,
                                        const char *description,
                                        const char *raw_text,
                                        char *summary,
                                        size_t summary_size);
bool tool_delegate_subagent_prefers_structured_output(delegate_subagent_kind_t kind);
