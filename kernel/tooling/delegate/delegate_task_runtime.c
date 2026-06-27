#include "delegate_task_store_internal.h"

#include "linux/kernel.h"
#include "linux/printk.h"

static const char *wake_state_name(delegate_wake_state_t state)
{
    switch (state) {
    case DELEGATE_WAKE_PENDING:
        return "pending";
    case DELEGATE_WAKE_DISPATCHED:
        return "dispatched";
    case DELEGATE_WAKE_COMPLETED:
        return "completed";
    case DELEGATE_WAKE_IDLE:
    default:
        return "idle";
    }
}

void bump_coordinator_visible_revision_locked(delegate_coordinator_record_t *coordinator)
{
    if (!coordinator) {
        return;
    }
    coordinator->visible_revision = s_visible_revision_seq++;
    if (coordinator->visible_revision == 0) {
        coordinator->visible_revision = s_visible_revision_seq++;
    }
    coordinator->changed = true;
}

err_t mutate_wake_state_locked(const char *coordinator_id,
                               delegate_wake_state_t state,
                               bool bump_retry,
                               err_t error,
                               bool record_success)
{
    int coord_idx = find_coordinator_index(coordinator_id);
    if (coord_idx < 0) {
        return ERR_NOT_FOUND;
    }

    delegate_coordinator_record_t *coordinator = &s_coordinators[coord_idx];
    bool state_changed = coordinator->wake_state != state;
    bool error_changed = strcmp(coordinator->wake_last_error,
                                error != 0 ? err_name(error) : "") != 0;
    if (!state_changed && !bump_retry && !record_success && !error_changed) {
        return 0;
    }
    coordinator->wake_state = state;
    coordinator->wake_last_attempt_ms = monotonic_ms_now();
    if (record_success) {
        coordinator->wake_last_success_ms = coordinator->wake_last_attempt_ms;
    }
    if (bump_retry) {
        coordinator->wake_retry_count++;
    }
    strscpy(coordinator->wake_last_error,
            error != 0 ? err_name(error) : "",
            sizeof(coordinator->wake_last_error));
    if (error != 0) {
        strscpy(coordinator->blocker_kind, "wake_retry", sizeof(coordinator->blocker_kind));
        strscpy(coordinator->blocker_text, err_name(error), sizeof(coordinator->blocker_text));
    } else if (state == DELEGATE_WAKE_DISPATCHED || state == DELEGATE_WAKE_COMPLETED) {
        coordinator->blocker_kind[0] = '\0';
        coordinator->blocker_text[0] = '\0';
    }
    pr_info("delegate_task_store: coordinator=%s wake_state=%s retries=%d err=%s",
            coordinator->coordinator_id,
            wake_state_name(coordinator->wake_state),
            coordinator->wake_retry_count,
            coordinator->wake_last_error[0] ? coordinator->wake_last_error : "OK");
    return 0;
}
