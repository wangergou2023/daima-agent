/* 消息总线：入/出队列与分发。 */

#include "bus.h"
#include "autoconf.h"
#include "os.h"
#include "linux/printk.h"
#include <string.h>

static const char *TAG = "bus";

static daima_queue_t *s_inbound_queue;
static daima_queue_t *s_outbound_queue;

daima_err_t message_bus_init(void)
{
    s_inbound_queue = daima_queue_create(DAIMA_BUS_QUEUE_LEN, sizeof(daima_msg_t));
    s_outbound_queue = daima_queue_create(DAIMA_BUS_QUEUE_LEN, sizeof(daima_msg_t));

    if (!s_inbound_queue || !s_outbound_queue) {
        DAIMA_LOGE(TAG, "Failed to create message queues");
        return DAIMA_ERR_NO_MEM;
    }

    DAIMA_LOGI(TAG, "Message bus initialized (queue depth %d)", DAIMA_BUS_QUEUE_LEN);
    return DAIMA_OK;
}

daima_err_t message_bus_push_inbound(const daima_msg_t *msg)
{
    if (!daima_queue_send(s_inbound_queue, msg, 1000)) {
        DAIMA_LOGW(TAG, "Inbound queue full, dropping message");
        return DAIMA_ERR_NO_MEM;
    }
    return DAIMA_OK;
}

daima_err_t message_bus_pop_inbound(daima_msg_t *msg, uint32_t timeout_ms)
{
    uint32_t wait_ms = (timeout_ms == UINT32_MAX) ? DAIMA_WAIT_FOREVER : timeout_ms;
    if (!daima_queue_receive(s_inbound_queue, msg, wait_ms)) {
        return DAIMA_ERR_TIMEOUT;
    }
    return DAIMA_OK;
}

daima_err_t message_bus_push_outbound(const daima_msg_t *msg)
{
    if (!daima_queue_send(s_outbound_queue, msg, 1000)) {
        DAIMA_LOGW(TAG, "Outbound queue full, dropping message");
        return DAIMA_ERR_NO_MEM;
    }
    return DAIMA_OK;
}

daima_err_t message_bus_pop_outbound(daima_msg_t *msg, uint32_t timeout_ms)
{
    uint32_t wait_ms = (timeout_ms == UINT32_MAX) ? DAIMA_WAIT_FOREVER : timeout_ms;
    if (!daima_queue_receive(s_outbound_queue, msg, wait_ms)) {
        return DAIMA_ERR_TIMEOUT;
    }
    return DAIMA_OK;
}
