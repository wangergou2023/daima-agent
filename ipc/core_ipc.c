/* 多核 IPC：每核独立消息队列 */
#include "linux/core_task.h"
#include "autoconf.h"
#include "os.h"
#include "linux/printk.h"
#include "linux/slab.h"
#include <string.h>
#include <time.h>

#define CORE_QUEUE_DEPTH 32

static queue_t *s_queues[CORE_MAX];

err_t core_ipc_init(void)
{
    for (int i = 0; i < CORE_MAX; i++) {
        s_queues[i] = queue_create(CORE_QUEUE_DEPTH, sizeof(struct core_task));
        if (!s_queues[i]) {
            pr_err("core_ipc: failed to create queue for core %d", i);
            return ERR_NO_MEM;
        }
    }
    pr_info("core_ipc: initialized %d core queues (depth %d)", CORE_MAX, CORE_QUEUE_DEPTH);
    return 0;
}

err_t core_send(int core_id, const struct core_task *task)
{
    if (core_id < 0 || core_id >= CORE_MAX || !task) return ERR_INVALID_ARG;
    return queue_send(s_queues[core_id], task, 1000) ? 0 : ERR_NO_MEM;
}

err_t core_recv(int core_id, struct core_task *task, uint32_t timeout_ms)
{
    if (core_id < 0 || core_id >= CORE_MAX || !task) return ERR_INVALID_ARG;
    uint32_t wait = (timeout_ms == UINT32_MAX) ? WAIT_FOREVER : timeout_ms;
    return queue_receive(s_queues[core_id], task, wait) ? 0 : ERR_TIMEOUT;
}

err_t core_reply(const struct core_task *task)
{
    /* 回复一律发回调度核 */
    return core_send(CORE_SCHEDULER, task);
}