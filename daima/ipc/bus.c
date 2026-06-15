/* 消息总线：入/出队列与分发。 */

#include "bus.h"
#include "autoconf.h"
#include "os.h"
#include "linux/printk.h"
#include <string.h>
static daima_queue_t *s_inbound_queue;
static daima_queue_t *s_outbound_queue;

err_t message_bus_init(void)
{
    s_inbound_queue = daima_queue_create(BUS_QUEUE_LEN, sizeof(struct message));
    s_outbound_queue = daima_queue_create(BUS_QUEUE_LEN, sizeof(struct message));

    if (!s_inbound_queue || !s_outbound_queue) {
        pr_err("Failed to create message queues");
        return ERR_NO_MEM;
    }

    pr_info("Message bus initialized (queue depth %d)", BUS_QUEUE_LEN);
    return 0;
}

err_t message_bus_push_inbound(const struct message *msg)
{
    if (!daima_queue_send(s_inbound_queue, msg, 1000)) {
        pr_warn("Inbound queue full, dropping message");
        return ERR_NO_MEM;
    }
    return 0;
}

err_t message_bus_pop_inbound(struct message *msg, uint32_t timeout_ms)
{
    uint32_t wait_ms = (timeout_ms == UINT32_MAX) ? WAIT_FOREVER : timeout_ms;
    if (!daima_queue_receive(s_inbound_queue, msg, wait_ms)) {
        return ERR_TIMEOUT;
    }
    return 0;
}

err_t message_bus_push_outbound(const struct message *msg)
{
    if (!daima_queue_send(s_outbound_queue, msg, 1000)) {
        pr_warn("Outbound queue full, dropping message");
        return ERR_NO_MEM;
    }
    return 0;
}

err_t message_bus_pop_outbound(struct message *msg, uint32_t timeout_ms)
{
    uint32_t wait_ms = (timeout_ms == UINT32_MAX) ? WAIT_FOREVER : timeout_ms;
    if (!daima_queue_receive(s_outbound_queue, msg, wait_ms)) {
        return ERR_TIMEOUT;
    }
    return 0;
}
