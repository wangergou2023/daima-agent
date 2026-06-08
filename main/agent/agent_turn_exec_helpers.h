#pragma once

#include <stddef.h>
#include <stdbool.h>

#include "bus/message_bus.h"
#include "llm/llm_proxy.h"
#include "daima_err.h"

typedef struct cJSON cJSON;

typedef struct {
    bool modified_code_files;
    bool saw_explicit_verification;
    bool unrecoverable_tool_protocol_error;
    char last_modified_path[256];
    char last_checkpoint_path[256];
    char tool_protocol_error_reason[256];
} turn_exec_stats_t;

cJSON *agent_turn_build_assistant_content(const llm_response_t *resp);
char *agent_turn_generate_forced_final_response(const char *system_prompt,
                                                cJSON *messages,
                                                const char *reason);
cJSON *agent_turn_build_tool_results(const llm_response_t *resp,
                                     const daima_msg_t *msg,
                                     char *tool_output,
                                     size_t tool_output_size,
                                     turn_exec_stats_t *stats);
void agent_turn_maybe_run_auto_verification(const turn_exec_stats_t *stats, char **io_final_text);
