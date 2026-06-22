/* 智能体主循环：执行核回复异步恢复 + 新消息同步处理 */
#include "loop.h"
#include "cancel.h"
#include "hooks.h"
#include "turn_common.h"
#include "context_compress.h"
#include "learning.h"
#include "turn_finish.h"
#include "turn_prepare.h"
#include "turn_pipeline.h"
#include "turn_resume.h"
#include "turn_run.h"
#include "state.h"
#include "runtime.h"
#include "bus.h"
#include "autoconf.h"
#include "linux/compiler.h"
#include "linux/printk.h"
#include "os.h"
#include "drivers/platform/platform.h"
#include "drivers/tool/tool_bus_view.h"
#include "drivers/tool/tool_types.h"
#include "drivers/channel/gateway/ws_server.h"
#include "cjson.h"
#include "linux/slab.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int agent_self_test(void);
char *agent_self_test_results_json(void);

#define TURN_BUF_SIZE 131072

/* 处理 !test 自检命令，返回 true 表示已处理（调用者应直接 return） */
static bool handle_self_test_command(struct message *msg)
{
	if (!msg->content || strncmp(msg->content, "!test", 5) != 0)
		return false;

	agent_self_test();
	char *json = agent_self_test_results_json();

	cJSON *root = cJSON_Parse(json);
	int total = 0, passed = 0;
	cJSON *t = cJSON_GetObjectItem(root, "total");
	cJSON *p = cJSON_GetObjectItem(root, "passed");
	if (cJSON_IsNumber(t)) total = t->valueint;
	if (cJSON_IsNumber(p)) passed = p->valueint;

	char buf[4096];
	int off = snprintf(buf, sizeof(buf),
			"🔍 自检完成：%d/%d 通过\n\n", passed, total);

	cJSON *items = cJSON_GetObjectItem(root, "items");
	if (items && cJSON_IsArray(items)) {
		cJSON *item;
		cJSON_ArrayForEach(item, items) {
			const char *name = cJSON_GetStringValue(
				cJSON_GetObjectItem(item, "name"));
			cJSON *ok = cJSON_GetObjectItem(item, "ok");
			if (name && (size_t)off < sizeof(buf) - 64) {
				off += snprintf(buf + off, sizeof(buf) - off,
						"%s %s\n",
						cJSON_IsTrue(ok) ? "✅" : "❌", name);
			}
		}
	}
	cJSON_Delete(root);

	struct message reply;
	memset(&reply, 0, sizeof(reply));
	strscpy(reply.channel, msg->channel, sizeof(reply.channel));
	strscpy(reply.chat_id, msg->chat_id, sizeof(reply.chat_id));
	reply.content = strdup(buf);
	message_bus_push_outbound(&reply);

	kfree(json);

	/* 推送真实测试消息——走完整 LLM + subagent 流程 */
	struct message test_msg;
	memset(&test_msg, 0, sizeof(test_msg));
	strscpy(test_msg.channel, msg->channel, sizeof(test_msg.channel));
	strscpy(test_msg.chat_id, msg->chat_id, sizeof(test_msg.chat_id));
	strscpy(test_msg.source, "internal", sizeof(test_msg.source));
	test_msg.content = strdup(
		"使用 delegate_task 工具，同时执行以下三个检查："
		"1) free -h 查看内存使用"
		"2) df -h 查看磁盘使用"
		"3) uname -a 查看系统版本"
		"每个子Agent负责一项，并行处理，完成后汇总输出。");
	message_bus_push_inbound(&test_msg);

	agent_cleanup_inbound_msg(msg);
	return true;
}

/* 通用前置处理：意图设置、状态重置、内部控制检查。
 * 返回 0 继续处理，非 0 表示已终止（调用者应 return） */
static err_t common_preamble(struct message *msg)
{
	msg->intent = INTENT_OPEN;
	agent_extension_state_reset();

	if (agent_hooks_trigger_intent(msg) != 0) {
		char *ft = NULL, *rt = NULL;
		agent_turn_finish(msg, &ft, &rt, ERR_FAIL, 0, false, false);
		return ERR_FAIL;
	}

	if (agent_msg_is_internal_control(msg)) {
		pr_info("Dropping internal control %s:%s", msg->channel, msg->chat_id);
		agent_cleanup_inbound_msg(msg);
		return ERR_FAIL;
	}

	return 0;
}

static void process_new_message(struct message *msg)
{
	if (handle_self_test_command(msg))
		return;

	if (common_preamble(msg) != 0)
		return;

	char *sp = platform_calloc(1, TURN_BUF_SIZE);
	char *hj = platform_calloc(1, TURN_BUF_SIZE);
	if (!sp || !hj) { kfree(sp); kfree(hj); return; }

	cJSON *messages = NULL;
	err_t err = agent_turn_prepare(msg, agent_extension_state_plan(),
					sp, TURN_BUF_SIZE, hj, TURN_BUF_SIZE, &messages);
	if (err == 0) err = agent_hooks_trigger_prepare(msg, sp, TURN_BUF_SIZE, messages);

	if (err == 0) {
		const char *tj = tool_bus_tools_json_for_channel(msg->channel);
		agent_run_prepared_turn(msg, sp, messages, tj, msg->chat_id, 0);
	} else {
		char *ft = NULL, *rt = NULL;
		agent_turn_finish(msg, &ft, &rt, err, 0, false, false);
	}

	cJSON_Delete(messages);
	kfree(sp); kfree(hj);
}

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
			process_new_message(&msg);
		} else {
			/* 无新消息也无回复，稍微休眠 */
			usleep(50000);  /* 50ms */
		}
	}
}

void agent_process_message(struct message *msg)
{
	process_new_message(msg);
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
