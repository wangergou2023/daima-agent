/* 单回合入口前置处理。 */

#pragma once

#include <stdbool.h>

#include "bus.h"
#include "err.h"

typedef struct {
	bool marker_found;
	int attach_task_hits;
	int launch_candidate_hits;
	int restore_queued_hits;
	bool multi_subagent_confirmed;
} self_test_log_probe_t;

bool agent_turn_handle_self_test_command(struct message *msg);
bool agent_turn_build_self_test_log_marker(char *buf, size_t size,
					   const char *chat_id);
bool agent_turn_probe_self_test_runtime_log(const char *runtime_log_path,
					    const char *log_marker,
					    self_test_log_probe_t *probe);
bool agent_turn_build_self_test_followup_prompt(char *buf, size_t size,
						const char *analysis_root,
						const char *runtime_log_path,
						const char *log_marker);
bool agent_turn_build_self_test_workspace_status(char *buf, size_t size,
						 const char *analysis_root,
						 bool repo_present_before,
						 bool repo_ready_after);
err_t agent_turn_validate_inbound_message(struct message *msg);
