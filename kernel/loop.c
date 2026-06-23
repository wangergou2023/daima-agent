/* 智能体主循环：执行核回复异步恢复 + 新消息同步处理 */
#include "loop.h"
#include "turn_entry.h"
#include "context_compress.h"
#include "learning.h"
#include "turn_resume.h"
#include "runtime.h"
#include "bus.h"
#include "autoconf.h"
#include "linux/printk.h"
#include "os.h"
#include <string.h>
#include <unistd.h>

static void agent_loop_task(void *arg)
{
	(void)arg;
	pr_info("Agent loop started (executor-reply multiplex mode)");

	while (1) {
		/* 1. 优先处理执行核回复 */
		agent_turn_resume_poll();

		/* 2. 检查新消息 */
		struct message msg;
		memset(&msg, 0, sizeof(msg));
		if (message_bus_pop_inbound(&msg, 0) == 0) {
			agent_turn_process_new_message(&msg);
		} else {
			/* 无新消息也无回复，稍微休眠 */
			usleep(50000);  /* 50ms */
		}
	}
}

void agent_process_message(struct message *msg)
{
	agent_turn_process_new_message(msg);
}

err_t agent_loop_init(void)
{
	err_t err = context_compressor_init();
	if (err != 0) return err;
	if (runtime_config_get_learning_review_enabled()) {
		err = learning_review_init();
		if (err != 0) return err;
	} else {
		pr_info("Learning review disabled");
	}
	pr_info("Agent loop initialized");
	return 0;
}

err_t agent_loop_start(void)
{
	const uint32_t stacks[] = { AGENT_STACK, 20480, 16384, 14336, 12288 };
	for (size_t i = 0; i < sizeof(stacks) / sizeof(stacks[0]); i++) {
		if (task_create(agent_loop_task, "agent_loop", stacks[i], NULL, AGENT_PRIO, NULL)) {
			pr_info("agent_loop created stack=%u", (unsigned)stacks[i]);
			return 0;
		}
		pr_warn("agent_loop create failed stack=%u retry", (unsigned)stacks[i]);
	}
	return ERR_FAIL;
}
