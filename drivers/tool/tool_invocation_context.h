#pragma once

#include "bus.h"
#include "drivers/llm/llm_proxy.h"

/* Returns a heap-allocated input JSON when middleware changes the tool call. */
char *tool_invocation_context_patch_input(const llm_tool_call_t *call, const struct message *msg);

/* Returns an alternate tool name when middleware reroutes execution. */
const char *tool_invocation_context_patch_tool_name(const llm_tool_call_t *call, const struct message *msg);

/* True when the user explicitly asks to split work across multiple subagents. */
bool tool_invocation_context_message_requests_multi_subagents(const struct message *msg);
bool tool_invocation_context_terminal_command_looks_broad_discovery(const char *command,
                                                                    const struct message *msg);
