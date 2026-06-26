#pragma once

#include "bus.h"
#include "drivers/llm/llm_proxy.h"

/* Returns a heap-allocated input JSON when middleware changes the tool call. */
char *tool_invocation_context_patch_input(const llm_tool_call_t *call, const struct message *msg);

/* Returns an alternate tool name when middleware reroutes execution. */
const char *tool_invocation_context_patch_tool_name(const llm_tool_call_t *call, const struct message *msg);
