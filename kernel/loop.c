/* 智能体主循环：执行核回复异步恢复 + 新消息同步处理 */
#include "loop.h"
#include "turn_entry.h"
#include "context_compress.h"
#include "learning.h"
#include "turn_resume.h"
#include "runtime.h"
#include "delegate_task_store.h"
#include "drivers/channel/gateway/ws_server.h"
#include "autoconf.h"
#include "linux/printk.h"
#include "linux/kernel.h"
#include "linux/slab.h"
#include "os.h"
#include <string.h>
#include <unistd.h>
#include "cjson.h"

static bool coordinator_chat_is_web_visible(const char *chat_id)
{
	return chat_id &&
	       chat_id[0] &&
	       strncmp(chat_id, "delegate_sync_", 14) != 0;
}

static void send_coordinator_update(const delegate_coordinator_record_t *record)
{
	if (!record || !coordinator_chat_is_web_visible(record->chat_id)) {
		return;
	}

	cJSON *agents = cJSON_CreateArray();
	if (!agents) {
		return;
	}

	for (int i = 0; i < record->agent_count; i++) {
		const delegate_coordinator_agent_view_t *agent = &record->agents[i];
		cJSON *item = cJSON_CreateObject();
		if (!item) {
			continue;
		}
		cJSON_AddStringToObject(item, "name", agent->description[0] ? agent->description : agent->subagent_type);
		cJSON_AddStringToObject(item, "status", agent->status);
		cJSON_AddStringToObject(item, "subagent_type", agent->subagent_type);
		cJSON_AddNumberToObject(item, "elapsed_ms", agent->elapsed_ms);
		if (agent->output[0]) {
			cJSON_AddStringToObject(item, "output_text", agent->output);
		}
		if (agent->target_files[0]) {
			cJSON_AddStringToObject(item, "target_files", agent->target_files);
		}
		cJSON_AddBoolToObject(item, "write_approved", agent->write_approved);
		cJSON_AddItemToArray(agents, item);
	}

	char *json = cJSON_PrintUnformatted(agents);
	cJSON_Delete(agents);
	if (!json) {
		return;
	}
	ws_server_send_coordinator_status(record->chat_id, json);
	ws_server_send_coordinator_output(record->chat_id, json);
	kfree(json);
}

static char *build_coordinator_completion_summary(const delegate_coordinator_record_t *record)
{
	if (!record || !record->coordinator_id[0]) {
		return NULL;
	}

	cJSON *root = cJSON_CreateObject();
	cJSON *items = cJSON_CreateArray();
	if (!root || !items) {
		cJSON_Delete(root);
		cJSON_Delete(items);
		return NULL;
	}

	cJSON_AddStringToObject(root, "coordinator_id", record->coordinator_id);
	cJSON_AddStringToObject(root, "status", record->status);
	for (int i = 0; i < record->agent_count; i++) {
		const delegate_coordinator_agent_view_t *agent = &record->agents[i];
		cJSON *item = cJSON_CreateObject();
		if (!item) {
			continue;
		}
		cJSON_AddStringToObject(item, "task_id", agent->task_id);
		cJSON_AddStringToObject(item, "subagent_type", agent->subagent_type);
		cJSON_AddStringToObject(item, "description", agent->description);
		cJSON_AddStringToObject(item, "status", agent->status);
		if (agent->output[0]) {
			cJSON_AddStringToObject(item, "output", agent->output);
		}
		if (agent->target_files[0]) {
			cJSON_AddStringToObject(item, "target_files", agent->target_files);
		}
		cJSON_AddBoolToObject(item, "write_approved", agent->write_approved);
		cJSON_AddItemToArray(items, item);
	}
	cJSON_AddItemToObject(root, "agents", items);

	char *payload = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	return payload;
}

static void maybe_send_coordinator_completion(delegate_coordinator_record_t *record)
{
	if (!record || !record->chat_id[0] || record->completion_notified) {
		return;
	}
	if (strcmp(record->status, "done") != 0 && strcmp(record->status, "failed") != 0) {
		return;
	}
	if (!coordinator_chat_is_web_visible(record->chat_id)) {
		delegate_task_store_mark_completion_notified(record->coordinator_id);
		return;
	}

	char *summary = build_coordinator_completion_summary(record);
	if (!summary) {
		return;
	}

	err_t err = ws_server_send_coordinator_done(record->chat_id, summary);
	if (err == 0 || err == ERR_NOT_FOUND) {
		delegate_task_store_mark_completion_notified(record->coordinator_id);
		if (err == 0) {
			pr_info("Coordinator completion queued for chat=%s coordinator=%s status=%s",
				record->chat_id, record->coordinator_id, record->status);
		} else {
			pr_info("Coordinator completion dropped for offline chat=%s coordinator=%s status=%s",
				record->chat_id, record->coordinator_id, record->status);
		}
	} else {
		kfree(summary);
		pr_warn("Coordinator completion queue failed for chat=%s coordinator=%s err=%s",
			record->chat_id, record->coordinator_id, err_name(err));
	}
}

void agent_loop_poll_delegate_coordinators(void)
{
	delegate_coordinator_record_t records[4];
	if (!delegate_task_store_poll_updates(records, 4)) {
		return;
	}
	for (size_t i = 0; i < sizeof(records) / sizeof(records[0]); i++) {
		if (!records[i].coordinator_id[0]) {
			continue;
		}
		send_coordinator_update(&records[i]);
		maybe_send_coordinator_completion(&records[i]);
	}
}

static void agent_loop_task(void *arg)
{
	(void)arg;
	pr_info("Agent loop started (executor-reply multiplex mode)");

	while (1) {
		/* 1. 优先处理执行核回复 */
		agent_turn_resume_poll();
		agent_loop_poll_delegate_coordinators();

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
