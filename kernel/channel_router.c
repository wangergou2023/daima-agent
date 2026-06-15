#include "channel_router.h"
#include "channel_runtime.h"
#include "turn_common.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "bus.h"
#include "autoconf.h"
#include "linux/printk.h"
#include "os.h"
#include "linux/slab.h"
static void dispatch_outbound_task(void *arg)
{
    (void)arg;
    pr_info("Outbound dispatch started");

    while (1) {
        struct message msg;
        if (message_bus_pop_outbound(&msg, UINT32_MAX) != 0) continue;

        pr_info("Dispatching response to %s:%s", msg.channel, msg.chat_id);

        err_t send_err = channel_runtime_dispatch_outbound(&msg);
        if (send_err != 0) {
            pr_warn("Outbound send failed for %s:%s: %s", msg.channel, msg.chat_id, err_name(send_err));
        }

        agent_cleanup_outbound_msg(&msg);
        kfree(msg.image_path);
    }
}

err_t channel_router_start(void)
{
    return task_create(
        dispatch_outbound_task, "outbound",
        OUTBOUND_STACK, NULL,
        OUTBOUND_PRIO, NULL)
        ? 0 : ERR_FAIL;
}
