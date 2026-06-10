#include "app/channel_router.h"
#include "app/channel_runtime.h"
#include "agent/agent_turn_common.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "bus/message_bus.h"
#include "daima_config.h"
#include "daima_log.h"
#include "daima_os.h"

static const char *TAG = "router";

static void dispatch_outbound_task(void *arg)
{
    (void)arg;
    DAIMA_LOGI(TAG, "Outbound dispatch started");

    while (1) {
        daima_msg_t msg;
        if (message_bus_pop_outbound(&msg, UINT32_MAX) != DAIMA_OK) continue;

        DAIMA_LOGI(TAG, "Dispatching response to %s:%s", msg.channel, msg.chat_id);

        daima_err_t send_err = channel_runtime_dispatch_outbound(&msg);
        if (send_err != DAIMA_OK) {
            DAIMA_LOGW(TAG, "Outbound send failed for %s:%s: %s",
                      msg.channel, msg.chat_id, daima_err_to_name(send_err));
        }

        agent_cleanup_outbound_msg(&msg);
        free(msg.image_path);
    }
}

daima_err_t channel_router_start(void)
{
    return daima_task_create(
        dispatch_outbound_task, "outbound",
        DAIMA_OUTBOUND_STACK, NULL,
        DAIMA_OUTBOUND_PRIO, NULL)
        ? DAIMA_OK : DAIMA_FAIL;
}
