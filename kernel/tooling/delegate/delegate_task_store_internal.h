#pragma once

#include "delegate_task_store.h"

#include "linux/mutex.h"

extern struct mutex s_delegate_mutex;
extern bool s_delegate_mutex_inited;
extern delegate_task_record_t s_records[DELEGATE_TASK_STORE_MAX];
extern delegate_coordinator_record_t s_coordinators[DELEGATE_COORDINATOR_STORE_MAX];
extern unsigned long s_visible_revision_seq;

long monotonic_ms_now(void);
void ensure_store_init(void);
int find_record_index(const char *task_id);
int find_record_index_by_session(const char *session_id);
int allocate_record_index(void);
int find_coordinator_index(const char *coordinator_id);
int allocate_coordinator_index(void);
void clear_record(delegate_task_record_t *record);
const char *task_status_name(delegate_task_status_t status);
bool filesets_overlap(const char *left, const char *right);
bool text_has_effective_output(const char *text);
void bump_coordinator_visible_revision_locked(delegate_coordinator_record_t *coordinator);
err_t mutate_wake_state_locked(const char *coordinator_id,
                               delegate_wake_state_t state,
                               bool bump_retry,
                               err_t error,
                               bool record_success);
void session_record_start(delegate_task_record_t *record);
void session_record_queued(delegate_task_record_t *record);
void session_record_running(delegate_task_record_t *record);
void session_record_blocked(delegate_task_record_t *record);
void session_refresh_pending_request(delegate_task_record_t *record);
void session_record_step(delegate_task_record_t *record,
                         const char *step_kind,
                         const char *detail,
                         const char *output_preview);
void session_record_message(delegate_task_record_t *record,
                            const char *message_kind,
                            const char *text);
void session_record_unblocked(delegate_task_record_t *record);
void session_record_done(delegate_task_record_t *record);
void refresh_coordinator_locked(delegate_coordinator_record_t *coordinator);
void poll_record_locked(delegate_task_record_t *record);
