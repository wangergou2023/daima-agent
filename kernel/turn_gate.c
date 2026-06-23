/* 单回合入口前置处理：自检命令与内部控制过滤。 */
#include "turn_gate.h"

#include "turn_common.h"
#include "cjson.h"
#include "linux/kernel.h"
#include "linux/printk.h"
#include "linux/slab.h"
#include <stdio.h>
#include <string.h>

int agent_self_test(void);
char *agent_self_test_results_json(void);

bool agent_turn_handle_self_test_command(struct message *msg)
{
	if (!msg->content || strncmp(msg->content, "!test", 5) != 0) {
		return false;
	}

	agent_self_test();
	char *json = agent_self_test_results_json();

	cJSON *root = cJSON_Parse(json);
	int total = 0;
	int passed = 0;
	cJSON *t = cJSON_GetObjectItem(root, "total");
	cJSON *p = cJSON_GetObjectItem(root, "passed");
	if (cJSON_IsNumber(t)) {
		total = t->valueint;
	}
	if (cJSON_IsNumber(p)) {
		passed = p->valueint;
	}

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
						cJSON_IsTrue(ok) ? "✅" : "❌",
						name);
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

err_t agent_turn_validate_inbound_message(struct message *msg)
{
	msg->intent = INTENT_OPEN;

	if (agent_msg_is_internal_control(msg)) {
		pr_info("Dropping internal control %s:%s", msg->channel, msg->chat_id);
		agent_cleanup_inbound_msg(msg);
		return ERR_FAIL;
	}

	return 0;
}
