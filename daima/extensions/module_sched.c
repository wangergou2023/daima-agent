#include "hooks.h"
#include "kernel/sched/sched.h"
#include "turn_persist.h"
#include "state.h"
#include "runtime.h"
#include "linux/module.h"
#include "linux/printk.h"
#include "drivers/platform/platform.h"

#include <stdlib.h>
#include <string.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("daima");
MODULE_DESCRIPTION("Agent Extension: coordinator");

static const char *TAG = "ext_coordinator";

static daima_err_t replace_run(daima_msg_t *msg, char *system_prompt,
                               cJSON *messages, const char *tools_json,
                               char **out_final_text)
{
    struct sched_runqueue rq;
    memset(&rq, 0, sizeof(rq));
    daima_err_t coord_err = sched_dispatch(msg->intent,
                                           agent_extension_state_plan(),
                                           msg->content,
                                           &rq);
    if (coord_err != DAIMA_OK) {
        DAIMA_LOGW(TAG, "Coordinator skipped: %s", daima_err_to_name(coord_err));
        sched_exit(&rq);
        return DAIMA_FAIL;
    }
    if (rq.nr_agents <= 1) {
        sched_exit(&rq);
        return DAIMA_FAIL;
    }

    DAIMA_LOGI(TAG, "Coordinator: launching %d sub-agents for intent=%s",
               rq.nr_agents, daima_intent_name(msg->intent));
    char thinking_msg[512];
    int off = snprintf(thinking_msg, sizeof(thinking_msg),
                       "🤖 Coordinator 并行处理中 (%d个子Agent", rq.nr_agents);
    for (int i = 0; i < rq.nr_agents && i < SCHED_MAX_AGENTS; i++) {
        off += snprintf(thinking_msg + off, sizeof(thinking_msg) - (size_t)off,
                        "%s%s", i == 0 ? ": " : " + ", sched_class_name(rq.agents[i].class));
    }
    snprintf(thinking_msg + off, sizeof(thinking_msg) - (size_t)off, ")");
    agent_turn_queue_outbound_text(msg, strdup(thinking_msg), NULL, true);

    rq.timeout_ms = runtime_config_get_request_timeout_ms() + 10000;
    sched_start(&rq, system_prompt, messages, tools_json);
    daima_err_t err = sched_wait(&rq);
    if (err == DAIMA_OK) {
        char *merged = daima_calloc(1, SCHED_MERGED_MAX);
        if (!merged) {
            sched_exit(&rq);
            return DAIMA_ERR_NO_MEM;
        }
        sched_merge(&rq, merged, SCHED_MERGED_MAX);
        if (merged[0] != '\0') {
            *out_final_text = merged;
            merged = NULL;
        }
        free(merged);
    } else {
        DAIMA_LOGW(TAG, "Coordinator launch skipped: %s", daima_err_to_name(err));
    }
    sched_exit(&rq);
    return DAIMA_OK;
}

static agent_extension_hooks_t ext = {
    .name = "coordinator",
    .replace_run = replace_run,
    .enabled = true,
};

static int __init sched_module_init(void)
{
    agent_hooks_register(&ext);
    return 0;
}

static void __exit sched_module_exit(void)
{
}

module_init(sched_module_init);
module_exit(sched_module_exit);
