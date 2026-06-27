/* delegate_task subagent metadata helpers */
#pragma once

#include "drivers/tool/tool_delegate_types.h"

delegate_subagent_kind_t tool_delegate_parse_subagent_kind(const char *subagent_type);
const char *tool_delegate_subagent_prompt_prefix(delegate_subagent_kind_t kind);
const char *tool_delegate_subagent_model_for_kind(delegate_subagent_kind_t kind);
