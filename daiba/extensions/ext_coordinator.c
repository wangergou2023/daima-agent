#include "core/hooks.h"
#include "core/agent_coordinator.h"
#include "core/agent_turn_persist.h"
#include "core/state.h"
#include "core/runtime.h"
#include "core/log.h"
#include "drivers/platform/platform.h"

#include <stdlib.h>
#include <string.h>

static const char *TAG = "ext_coordinator";

static daima_err_t replace_run(daima_msg_t *msg, char *system_prompt,
                               cJSON *messages, const char *tools_json,
                               char **out_final_text)
{
    coordinator_t coord;
    memset(&coord, 0, sizeof(coord));
    daima_err_t coord_err = coordinator_decompose(msg->intent,
                                                  agent_extension_state_plan(),
                                                  msg->content,
                                                  &coord);
    if (coord_err != DAIMA_OK) {
        DAIMA_LOGW(TAG, "Coordinator skipped: %s", daima_err_to_name(coord_err));
        coordinator_free(&coord);
        return DAIMA_FAIL;
    }
    if (coord.agent_count <= 1) {
        coordinator_free(&coord);
        return DAIMA_FAIL;
    }

    DAIMA_LOGI(TAG, "Coordinator: launching %d sub-agents for intent=%s",
               coord.agent_count, daima_intent_name(msg->intent));
    char thinking_msg[512];
    int off = snprintf(thinking_msg, sizeof(thinking_msg),
                       "🤖 Coordinator 并行处理中 (%d个子Agent", coord.agent_count);
    for (int i = 0; i < coord.agent_count && i < COORDINATOR_MAX_SUB_AGENTS; i++) {
        off += snprintf(thinking_msg + off, sizeof(thinking_msg) - (size_t)off,
                        "%s%s", i == 0 ? ": " : " + ", agent_role_name(coord.agents[i].role));
    }
    snprintf(thinking_msg + off, sizeof(thinking_msg) - (size_t)off, ")");
    agent_turn_queue_outbound_text(msg, strdup(thinking_msg), NULL, true);

    daima_err_t err = coordinator_launch_all(system_prompt, messages, tools_json, &coord);
    if (err == DAIMA_OK) {
        int coord_timeout = runtime_config_get_request_timeout_ms() + 10000;
        coordinator_wait_all(&coord, coord_timeout);
        char *merged = daima_calloc(1, COORDINATOR_RESULT_MAX * COORDINATOR_MAX_SUB_AGENTS);
        if (merged) {
            coordinator_merge_results(&coord, merged,
                                      COORDINATOR_RESULT_MAX * COORDINATOR_MAX_SUB_AGENTS);
            if (merged[0] != '\0') {
                *out_final_text = merged;
                merged = NULL;
            }
            free(merged);
        } else {
            err = DAIMA_ERR_NO_MEM;
        }
    } else {
        DAIMA_LOGW(TAG, "Coordinator launch skipped: %s", daima_err_to_name(err));
    }
    coordinator_free(&coord);
    return DAIMA_OK;
}

static agent_extension_hooks_t ext = {
    .name = "coordinator",
    .replace_run = replace_run,
    .enabled = true,
};

__attribute__((constructor)) static void register_ext(void)
{
    agent_hooks_register(&ext);
}
