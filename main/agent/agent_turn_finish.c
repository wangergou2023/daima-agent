#include "agent/agent_turn_finish.h"

#include <stdlib.h>

#include "agent/agent_turn_common.h"
#include "agent/agent_turn_persist.h"
#include "daima_log.h"
#include "daima_os.h"
#include "daima_platform.h"

static const char *TAG = "agent_finish";

void agent_turn_finish(
    daima_msg_t *msg,
    char **io_final_text,
    daima_err_t turn_err,
    int iteration,
    bool tool_budget_exhausted)
{
    char *final_text = io_final_text ? *io_final_text : NULL;

    if (final_text && final_text[0]) {
        agent_turn_save_session(msg, final_text, iteration);
        agent_turn_queue_outbound_text(msg, final_text, true);
        final_text = NULL;
    } else {
        free(final_text);
        final_text = agent_turn_build_error_reply(tool_budget_exhausted);
        if (final_text) {
            agent_turn_queue_outbound_text(msg, final_text, true);
            final_text = NULL;
        }
    }

    if (io_final_text) {
        *io_final_text = final_text;
    }

    agent_cleanup_inbound_msg(msg);

    if (turn_err != DAIMA_OK) {
        DAIMA_LOGE(TAG, "Agent turn failed: %s", daima_err_to_name(turn_err));
    }

    DAIMA_LOGI(TAG, "Free memory: %d bytes", (int)daima_get_free_memory());
}
