#pragma once

#ifdef __cplusplus
extern "C" {
#endif

int irq_init(void);
void irq_handle_agent_signal(int signum);

#ifdef __cplusplus
}
#endif
