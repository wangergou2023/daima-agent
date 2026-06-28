#include "drivers/tool/tool_delegate_lifecycle.h"

#include <string.h>

#include "delegate/delegate_task_store.h"
#include "delegate/delegate_parent_wake.h"
#include "drivers/tool/tool_delegate_dispatch.h"
#include "linux/kernel.h"

static bool delegate_lifecycle_should_scan_coordinator(const delegate_coordinator_record_t *record)
{
    if (!record || !record->coordinator_id[0]) {
        return false;
    }
    if (record->queued_count <= 0) {
        return false;
    }
    if (strcmp(record->status, "done") == 0 ||
        strcmp(record->status, "failed") == 0) {
        return false;
    }
    return true;
}

err_t delegate_launch_ready_background_subagents_for_runtime(void)
{
    delegate_coordinator_record_t active[DELEGATE_COORDINATOR_STORE_MAX];
    err_t first_err = 0;

    memset(active, 0, sizeof(active));
    if (!delegate_task_store_list_active_coordinators(active, ARRAY_SIZE(active))) {
        return 0;
    }

    for (size_t i = 0; i < ARRAY_SIZE(active); i++) {
        if (!delegate_lifecycle_should_scan_coordinator(&active[i])) {
            continue;
        }
        err_t err = tool_delegate_launch_one_ready_background_subagent(active[i].coordinator_id,
                                                                       active[i].chat_id);
        if (err != 0 && first_err == 0) {
            first_err = err;
        }
    }
    return first_err;
}

err_t delegate_lifecycle_poll_runtime(void)
{
    err_t err = delegate_launch_ready_background_subagents_for_runtime();
    delegate_parent_wake_poll();
    return err;
}
