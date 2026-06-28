/* 轻量委托任务状态表：为 delegate_task 提供后台任务句柄与轮询。 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "err.h"

#define DELEGATE_TASK_ID_LEN 32
#define DELEGATE_COORDINATOR_ID_LEN 32
#define DELEGATE_TASK_DESC_LEN 64
#define DELEGATE_TASK_AGENT_LEN 24
#define DELEGATE_TASK_KEY_LEN 48
#define DELEGATE_TASK_PROMPT_LEN 2048
#define DELEGATE_TASK_SCOPE_PATH_LEN 512
#define DELEGATE_TASK_SCOPE_KIND_LEN 32
#define DELEGATE_TASK_ANALYSIS_FOCUS_LEN 48
#define DELEGATE_TASK_DEPENDS_ON_LEN 192
#define DELEGATE_PREFLIGHT_TOOL_NAME_LEN 32
#define DELEGATE_PREFLIGHT_TOOL_INPUT_LEN 1024
#define DELEGATE_TASK_OUTPUT_LEN 8192
#define DELEGATE_TASK_FILESET_LEN 256
#define DELEGATE_BLOCKER_KIND_LEN 24
#define DELEGATE_BLOCKER_TEXT_LEN 128
#define DELEGATE_REQUEST_TYPE_LEN 24
#define DELEGATE_REQUEST_ID_LEN 64
#define DELEGATE_REQUEST_PROMPT_LEN 256
#define DELEGATE_TEAM_NAME_LEN 48
#define DELEGATE_TEAM_RUN_ID_LEN 32
#define DELEGATE_COORDINATOR_AGENTS_MAX 16
#define DELEGATE_TASK_STORE_MAX 128
#define DELEGATE_COORDINATOR_STORE_MAX 32
#define DELEGATE_SESSION_SUMMARY_LEN 320
#define DELEGATE_SESSION_FRAME_LIMIT 80
#define DELEGATE_SESSION_COMMIT_LIMIT 80

typedef struct {
    char id[64];
    unsigned long seq;
    char type[32];
    char status[16];
    char phase[16];
    char task[DELEGATE_TASK_DESC_LEN];
    char detail[192];
    char output_preview[256];
    char blocker_kind[DELEGATE_BLOCKER_KIND_LEN];
    char blocker_text[DELEGATE_BLOCKER_TEXT_LEN];
    long ts_ms;
} delegate_session_frame_t;

typedef struct {
    char id[64];
    unsigned long seq;
    char kind[16];
    char phase[16];
    char status[16];
    char label[DELEGATE_TASK_DESC_LEN];
    char text[256];
    long ts_ms;
} delegate_session_commit_t;

typedef struct {
    char request_type[DELEGATE_REQUEST_TYPE_LEN];
    char request_id[DELEGATE_REQUEST_ID_LEN];
    char prompt_text[DELEGATE_REQUEST_PROMPT_LEN];
} delegate_session_pending_request_t;

typedef struct {
    char summary[DELEGATE_SESSION_SUMMARY_LEN];
    int permission_count;
    int question_count;
    unsigned long frame_seq_next;
    unsigned long commit_seq_next;
    delegate_session_pending_request_t permissions[2];
    delegate_session_pending_request_t questions[2];
    int frame_count;
    delegate_session_frame_t frames[DELEGATE_SESSION_FRAME_LIMIT];
    int commit_count;
    delegate_session_commit_t commits[DELEGATE_SESSION_COMMIT_LIMIT];
} delegate_child_session_view_t;

typedef struct {
    char request_type[DELEGATE_REQUEST_TYPE_LEN];
    char request_id[DELEGATE_REQUEST_ID_LEN];
    char prompt_text[DELEGATE_REQUEST_PROMPT_LEN];
} delegate_pending_request_view_t;

typedef enum {
    DELEGATE_TASK_QUEUED = 0,
    DELEGATE_TASK_RUNNING,
    DELEGATE_TASK_DONE,
    DELEGATE_TASK_FAILED,
} delegate_task_status_t;

typedef enum {
    DELEGATE_WAKE_IDLE = 0,
    DELEGATE_WAKE_PENDING,
    DELEGATE_WAKE_DISPATCHED,
    DELEGATE_WAKE_COMPLETED,
} delegate_wake_state_t;

typedef struct {
    char tool_name[DELEGATE_PREFLIGHT_TOOL_NAME_LEN];
    char input_json[DELEGATE_PREFLIGHT_TOOL_INPUT_LEN];
    bool continue_on_error;
} delegate_preflight_tool_view_t;

typedef struct {
    char task_id[DELEGATE_TASK_ID_LEN];
    char coordinator_id[DELEGATE_COORDINATOR_ID_LEN];
    char session_id[32];
    char subagent_type[DELEGATE_TASK_AGENT_LEN];
    char task_key[DELEGATE_TASK_KEY_LEN];
    char description[DELEGATE_TASK_DESC_LEN];
    char prompt[DELEGATE_TASK_PROMPT_LEN];
    char model[64];
    char scope_path[DELEGATE_TASK_SCOPE_PATH_LEN];
    char scope_kind[DELEGATE_TASK_SCOPE_KIND_LEN];
    char analysis_focus[DELEGATE_TASK_ANALYSIS_FOCUS_LEN];
    char depends_on[DELEGATE_TASK_DEPENDS_ON_LEN];
    delegate_preflight_tool_view_t preflight_tool;
    delegate_task_status_t status;
    err_t error;
    long started_ms;
    long finished_ms;
    bool write_approved;
    char target_files[DELEGATE_TASK_FILESET_LEN];
    char blocker_kind[DELEGATE_BLOCKER_KIND_LEN];
    char blocker_text[DELEGATE_BLOCKER_TEXT_LEN];
    delegate_pending_request_view_t pending_request;
    char output[DELEGATE_TASK_OUTPUT_LEN];
    delegate_child_session_view_t child_session;
} delegate_task_record_t;

typedef struct {
    char task_id[DELEGATE_TASK_ID_LEN];
    char session_id[32];
    char subagent_type[DELEGATE_TASK_AGENT_LEN];
    char task_key[DELEGATE_TASK_KEY_LEN];
    char description[DELEGATE_TASK_DESC_LEN];
    char model[64];
    char scope_path[DELEGATE_TASK_SCOPE_PATH_LEN];
    char scope_kind[DELEGATE_TASK_SCOPE_KIND_LEN];
    char analysis_focus[DELEGATE_TASK_ANALYSIS_FOCUS_LEN];
    char depends_on[DELEGATE_TASK_DEPENDS_ON_LEN];
    delegate_preflight_tool_view_t preflight_tool;
    char status[16];
    long elapsed_ms;
    bool write_approved;
    char target_files[DELEGATE_TASK_FILESET_LEN];
    char blocker_kind[DELEGATE_BLOCKER_KIND_LEN];
    char blocker_text[DELEGATE_BLOCKER_TEXT_LEN];
} delegate_coordinator_agent_view_t;

typedef struct {
    char coordinator_id[DELEGATE_COORDINATOR_ID_LEN];
    char chat_id[64];
    char team_run_id[DELEGATE_TEAM_RUN_ID_LEN];
    char team_name[DELEGATE_TEAM_NAME_LEN];
    char dispatch_mode[24];
    char status[16];
    int agent_count;
    int completed_count;
    int running_count;
    int queued_count;
    int blocked_count;
    int failed_count;
    int effective_output_count;
    bool changed;
    bool completion_notified;
    bool parent_response_sent;
    bool parent_resume_enqueued;
    unsigned long visible_revision;
    unsigned long last_sent_revision;
    delegate_wake_state_t wake_state;
    int wake_retry_count;
    long wake_last_attempt_ms;
    long wake_last_success_ms;
    char wake_last_error[32];
    char blocker_kind[DELEGATE_BLOCKER_KIND_LEN];
    char blocker_text[DELEGATE_BLOCKER_TEXT_LEN];
    delegate_coordinator_agent_view_t agents[DELEGATE_COORDINATOR_AGENTS_MAX];
} delegate_coordinator_record_t;

typedef struct {
    char chat_id[64];
    char coordinator_id[DELEGATE_COORDINATOR_ID_LEN];
    char team_run_id[DELEGATE_TEAM_RUN_ID_LEN];
    char team_name[DELEGATE_TEAM_NAME_LEN];
    char dispatch_mode[24];
    char status[16];
    int agent_count;
    int completed_count;
    int running_count;
    int queued_count;
    int blocked_count;
    int failed_count;
    int effective_output_count;
    bool completion_notified;
    bool parent_response_sent;
    bool parent_resume_enqueued;
    delegate_wake_state_t wake_state;
    int wake_retry_count;
    long wake_last_attempt_ms;
    long wake_last_success_ms;
    char wake_last_error[32];
} delegate_parent_coordinator_list_item_t;

typedef struct {
    char task_id[DELEGATE_TASK_ID_LEN];
    char coordinator_id[DELEGATE_COORDINATOR_ID_LEN];
    char session_id[32];
    char subagent_type[DELEGATE_TASK_AGENT_LEN];
    char task_key[DELEGATE_TASK_KEY_LEN];
    char description[DELEGATE_TASK_DESC_LEN];
    char model[64];
    char scope_path[DELEGATE_TASK_SCOPE_PATH_LEN];
    char scope_kind[DELEGATE_TASK_SCOPE_KIND_LEN];
    char analysis_focus[DELEGATE_TASK_ANALYSIS_FOCUS_LEN];
    char depends_on[DELEGATE_TASK_DEPENDS_ON_LEN];
    char status[16];
    long started_ms;
    long finished_ms;
    long elapsed_ms;
    bool write_approved;
    char target_files[DELEGATE_TASK_FILESET_LEN];
    char blocker_kind[DELEGATE_BLOCKER_KIND_LEN];
    char blocker_text[DELEGATE_BLOCKER_TEXT_LEN];
    char output[DELEGATE_TASK_OUTPUT_LEN];
} delegate_parent_task_list_item_t;

typedef struct {
    char chat_id[64];
    int coordinator_count;
    int task_count;
    delegate_parent_coordinator_list_item_t coordinators[DELEGATE_COORDINATOR_STORE_MAX];
    delegate_parent_task_list_item_t tasks[DELEGATE_TASK_STORE_MAX];
} delegate_parent_registry_view_t;

typedef struct {
    char task_id[DELEGATE_TASK_ID_LEN];
    char session_id[32];
    char coordinator_id[DELEGATE_COORDINATOR_ID_LEN];
    char parent_chat_id[64];
} delegate_parent_route_view_t;

err_t delegate_task_store_init(void);
err_t delegate_task_store_start(const char *task_id,
                                const char *coordinator_id,
                                const char *session_id,
                                const char *subagent_type,
                                const char *task_key,
                                const char *description,
                                const char *prompt,
                                const char *model,
                                const char *scope_path,
                                const char *scope_kind,
                                const char *analysis_focus,
                                const delegate_preflight_tool_view_t *preflight_tool);
err_t delegate_task_store_plan(const char *task_id,
                               const char *coordinator_id,
                               const char *session_id,
                               const char *subagent_type,
                               const char *task_key,
                               const char *description,
                               const char *prompt,
                               const char *model,
                               const char *scope_path,
                               const char *scope_kind,
                               const char *analysis_focus,
                               const char *depends_on,
                               const delegate_preflight_tool_view_t *preflight_tool);
err_t delegate_task_store_mark_running(const char *task_id);
bool delegate_task_store_claim_queued_task(const char *task_id);
err_t delegate_task_store_complete(const char *task_id,
                                   const char *output,
                                   const char *target_files,
                                   bool write_approved);
err_t delegate_task_store_fail(const char *task_id,
                               err_t error,
                               const char *output);
err_t delegate_task_store_mark_blocked(const char *task_id,
                                       const char *blocker_kind,
                                       const char *blocker_text,
                                       const char *output);
err_t delegate_task_store_clear_blocked(const char *task_id);
err_t delegate_task_store_set_pending_request(const char *task_id,
                                              const char *request_type,
                                              const char *request_id,
                                              const char *prompt_text);
err_t delegate_task_store_append_session_step(const char *task_id,
                                              const char *step_kind,
                                              const char *detail,
                                              const char *output_preview);
err_t delegate_task_store_append_session_message(const char *task_id,
                                                 const char *message_kind,
                                                 const char *text);
err_t delegate_task_store_poll(const char *task_id,
                               delegate_task_record_t *out);
err_t delegate_task_store_snapshot(const char *task_id,
                                   delegate_task_record_t *out);
err_t delegate_task_store_start_coordinator(const char *coordinator_id,
                                            const char *chat_id,
                                            const char *team_run_id,
                                            const char *team_name,
                                            const char *dispatch_mode);
err_t delegate_task_store_attach_task(const char *coordinator_id,
                                      const char *task_id);
err_t delegate_task_store_poll_coordinator(const char *coordinator_id,
                                           delegate_coordinator_record_t *out);
err_t delegate_task_store_mark_completion_notified(const char *coordinator_id);
err_t delegate_task_store_mark_parent_response_sent(const char *chat_id);
err_t delegate_task_store_mark_parent_resume_enqueued(const char *coordinator_id);
err_t delegate_task_store_mark_wake_pending(const char *coordinator_id);
err_t delegate_task_store_mark_wake_dispatched(const char *coordinator_id);
err_t delegate_task_store_mark_wake_retry(const char *coordinator_id, err_t error);
err_t delegate_task_store_mark_wake_completed(const char *coordinator_id);
err_t delegate_task_store_mark_visible_revision_sent(const char *coordinator_id,
                                                     unsigned long visible_revision);
bool delegate_task_store_poll_updates(delegate_coordinator_record_t *out,
                                      size_t max_out);
bool delegate_task_store_drain_changed_coordinators(delegate_coordinator_record_t *out,
                                                    size_t max_out);
bool delegate_task_store_peek_changed_coordinators(delegate_coordinator_record_t *out,
                                                   size_t max_out);
bool delegate_task_store_list_active_coordinators(delegate_coordinator_record_t *out,
                                                  size_t max_out);
err_t delegate_task_store_snapshot_coordinator(const char *coordinator_id,
                                               delegate_coordinator_record_t *out);
err_t delegate_task_store_snapshot_parent(const char *chat_id,
                                          delegate_parent_registry_view_t *out);
err_t delegate_task_store_find_by_session(const char *session_id,
                                          delegate_task_record_t *task_out,
                                          delegate_coordinator_record_t *coordinator_out);
err_t delegate_task_store_find_parent_route_by_session(const char *session_id,
                                                       delegate_parent_route_view_t *out);
int delegate_task_store_running_count(void);
int delegate_task_store_running_count_for_parent(const char *chat_id);
void delegate_task_store_reset_for_test(void);
