#pragma once

#include <stdbool.h>

#include "delegate_task_store.h"

const char *delegate_wake_state_name(delegate_wake_state_t state);

char *delegate_coordinator_snapshot_json_build(const delegate_coordinator_record_t *record,
                                               bool include_full_output);

char *delegate_coordinator_completion_json_build(const delegate_coordinator_record_t *record);

char *delegate_subagent_session_payload_json_build(const delegate_coordinator_record_t *record,
                                                   const delegate_coordinator_agent_view_t *agent);

char *delegate_subagent_session_delta_json_build(const char *task_id,
                                                 unsigned long history_after_seq,
                                                 unsigned long frame_after_seq,
                                                 unsigned long commit_after_seq);
char *delegate_subagent_session_deltas_json_build(const char *chat_id,
                                                  const char *request_json);
char *delegate_parent_subagent_state_delta_json_build(const char *chat_id,
                                                      unsigned long after_visible_revision,
                                                      const char *request_json);

char *delegate_parent_subagent_state_json_build(const char *chat_id);
