/* 信号处理框架——存根实现。
 * 对齐 Linux kernel/irq/，记录最后一次 agent 信号供查询。 */

#include "kernel/irq/irq.h"

#include <signal.h>

static volatile sig_atomic_t s_last_agent_signal;

int irq_init(void)
{
    s_last_agent_signal = 0;
    return 0;
}

void irq_handle_agent_signal(int signum)
{
    s_last_agent_signal = (sig_atomic_t)signum;
}
