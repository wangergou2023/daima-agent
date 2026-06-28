#pragma once

#include "bus.h"
#include "drivers/llm/llm_proxy.h"

/* Returns a heap-allocated input JSON when middleware changes the tool call. */
char *tool_invocation_context_patch_input(const llm_tool_call_t *call, const struct message *msg);

/* Returns an alternate tool name when middleware reroutes execution. */
const char *tool_invocation_context_patch_tool_name(const llm_tool_call_t *call, const struct message *msg);

/* True when the user explicitly asks to split work across multiple subagents. */
bool tool_invocation_context_message_requests_multi_subagents(const struct message *msg);
bool tool_invocation_context_message_is_delegate_subagent(const struct message *msg);
bool tool_invocation_context_message_prefers_parallel_subagents(const struct message *msg);
bool tool_invocation_context_message_should_offer_delegate_tool(const struct message *msg);
bool tool_invocation_context_delegate_scope_path(const struct message *msg,
                                                 char *scope_path,
                                                 size_t scope_path_size);
