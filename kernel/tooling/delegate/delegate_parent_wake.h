/* Delegate parent wake manager: queue, flush, and retry coordinator updates. */
#pragma once

#include "delegate_task_store.h"

typedef err_t (*delegate_parent_wake_sender_fn_t)(const char *chat_id, const char *payload);
typedef err_t (*delegate_parent_wake_subagent_sender_fn_t)(const char *chat_id,
                                                           const char *event_type,
                                                           const char *task_id,
                                                           const char *task_key,
                                                           const char *session_id,
                                                           const char *coordinator_id,
                                                           unsigned long visible_revision,
                                                           const char *subagent_type,
                                                           const char *status,
                                                           const char *task,
                                                           const char *detail,
                                                           const char *output,
                                                           const char *visible_output,
                                                           const char *target_files,
                                                           const char *scope_path,
                                                           const char *scope_kind,
                                                           const char *analysis_focus,
                                                           const char *blocker_kind,
                                                           const char *blocker_text,
                                                           const char *blocker_scope);

err_t delegate_parent_wake_init(void);
void delegate_parent_wake_poll(void);
bool delegate_parent_wake_is_idle(void);
void delegate_parent_wake_record_parent_activity(const char *chat_id);
void delegate_parent_wake_replay_chat(const char *chat_id);
void delegate_parent_wake_reset_for_test(void);
void delegate_parent_wake_set_sender_overrides_for_test(delegate_parent_wake_sender_fn_t status_sender,
                                                        delegate_parent_wake_sender_fn_t output_sender,
                                                        delegate_parent_wake_sender_fn_t done_sender,
                                                        delegate_parent_wake_subagent_sender_fn_t subagent_sender);
void delegate_parent_wake_set_activity_window_for_test(long activity_window_ms);
int delegate_parent_wake_pending_count_for_test(void);
