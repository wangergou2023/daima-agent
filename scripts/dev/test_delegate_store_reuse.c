#include <stdio.h>
#include <string.h>

#include "err.h"
#include "kernel/tooling/delegate/delegate_task_store.h"

static int fail(const char *message)
{
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

int main(void)
{
    delegate_task_store_reset_for_test();

    for (int i = 0; i < DELEGATE_COORDINATOR_STORE_MAX; i++) {
        char coordinator_id[DELEGATE_COORDINATOR_ID_LEN];
        char chat_id[64];
        char task_id[DELEGATE_TASK_ID_LEN];
        char session_id[32];
        char scope_path[DELEGATE_TASK_SCOPE_PATH_LEN];

        snprintf(coordinator_id, sizeof(coordinator_id), "dc_t_%d", i);
        snprintf(chat_id, sizeof(chat_id), "chat_t_%d", i);
        snprintf(task_id, sizeof(task_id), "dt_t_%d", i);
        snprintf(session_id, sizeof(session_id), "delegate_sync_t_%d", i);
        snprintf(scope_path, sizeof(scope_path), "/tmp/test-scope-%d", i);

        if (delegate_task_store_start_coordinator(coordinator_id, chat_id, "tr_t", "team", "parallel") != 0) {
            return fail("unable to seed coordinator slots");
        }
        if (delegate_task_store_plan(task_id,
                                     coordinator_id,
                                     session_id,
                                     "explore",
                                     "task_key",
                                     "desc",
                                     "prompt",
                                     "model",
                                     scope_path,
                                     "subsystem",
                                     "bounded_deep_analysis",
                                     "",
                                     NULL) != 0) {
            return fail("unable to seed task record");
        }
        if (delegate_task_store_attach_task(coordinator_id, task_id) != 0) {
            return fail("unable to attach seeded task");
        }
        if (delegate_task_store_complete(task_id, "done", "", false) != 0) {
            return fail("unable to complete seeded task");
        }
        if (delegate_task_store_mark_completion_notified(coordinator_id) != 0) {
            return fail("unable to mark completion notified");
        }
        if (delegate_task_store_mark_parent_response_sent(chat_id) != 0) {
            return fail("unable to mark parent response sent");
        }
        if (delegate_task_store_mark_wake_completed(coordinator_id) != 0) {
            return fail("unable to mark wake completed");
        }
    }

    if (delegate_task_store_pending_coordinator_count() != 0) {
        return fail("terminal coordinators should not stay pending after completion markers");
    }

    if (delegate_task_store_start_coordinator("dc_reused", "chat_reused", "tr_reused", "team", "parallel") != 0) {
        return fail("coordinator slot was not reclaimed");
    }

    puts("PASS");
    return 0;
}
