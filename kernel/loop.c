/* 智能体主循环：执行核回复异步恢复 + 新消息同步处理 */
#include "loop.h"
#include "turn_entry.h"
#include "turn_persist.h"
#include "context_compress.h"
#include "learning.h"
#include "turn_resume.h"
#include "runtime.h"
#include "transcript.h"
#include "registry/registry.h"
#include "hr/hr_pipeline.h"
#include "delegate/delegate_parent_wake.h"
#include "drivers/tool/tool_delegate_lifecycle.h"
#include "drivers/channel/gateway/ws_server.h"
#include "autoconf.h"
#include "linux/printk.h"
#include "linux/kernel.h"
#include "linux/slab.h"
#include "os.h"
#include <unistd.h>
#include <string.h>

void agent_loop_poll_delegate_coordinators(void)
{
	delegate_lifecycle_poll_runtime();
}

static void agent_loop_task(void *arg)
{
	(void)arg;
	pr_info("Agent loop started (executor-reply multiplex mode)");

	/* HR 自动触发状态 */
	time_t last_hr_run = time(NULL);  /* 启动时标记，避免立即触发 */
	unsigned tick = 0;
#define HR_AUTO_CHECK_INTERVAL_TICKS  1200   /* 1200 * 50ms = 60s */
#define HR_AUTO_MIN_INTERVAL_SEC      1800   /* 至少间隔 30 分钟 */
#define HR_AUTO_MIN_NEW_COUNT         5      /* 至少 5 条新 transcript */

	while (1) {
		/* 1. 优先处理执行核回复 */
		agent_turn_resume_poll();
		agent_loop_poll_delegate_coordinators();

		/* 2. 检查新消息 */
		struct message msg;
		memset(&msg, 0, sizeof(msg));
		if (message_bus_pop_inbound(&msg, 0) == 0) {
			bool is_hr = msg.agent_id[0] && strcmp(msg.agent_id, "hr") == 0;

			/* HR 内部命令 */
			if (msg.content && (strncmp(msg.content, "hr ", 3) == 0 || is_hr)) {
				const char *cmd = msg.content;
				if (strncmp(cmd, "hr scan --auto", 14) == 0 ||
				    (is_hr && strncmp(cmd, "scan --auto", 11) == 0)) {
					int registered = 0;
					hr_run_pipeline(true, &registered);
					char *resp = kmalloc(256, GFP_KERNEL);
					if (resp) {
						snprintf(resp, 256, "HR scan complete. %d agent(s) registered.", registered);
						agent_turn_queue_outbound_text(&msg, resp, NULL, true);
					}
				} else if (strncmp(cmd, "hr scan", 7) == 0 ||
				           (is_hr && strncmp(cmd, "scan", 4) == 0)) {
					int registered = 0;
					hr_run_pipeline(false, &registered);
					char *resp = kmalloc(512, GFP_KERNEL);
					if (resp) {
						snprintf(resp, 512, "HR scan complete. %d agent(s) candidate.", registered);
						agent_turn_queue_outbound_text(&msg, resp, NULL, true);
					}
				} else if (strncmp(cmd, "hr list", 7) == 0 ||
				           (is_hr && strncmp(cmd, "list", 4) == 0)) {
					agent_definition_t agents[AGENT_PROFILES_MAX];
					int count = 0;
					agent_registry_list(false, agents, AGENT_PROFILES_MAX, &count);
					char *resp = kmalloc(4096, GFP_KERNEL);
					if (resp) {
						int off = snprintf(resp, 4096, "Specialists (%d):\n", count);
						for (int i = 0; i < count && off < 4000; i++) {
							off += snprintf(resp + off, (size_t)(4096 - off),
									"  %s: %s [%s]\n",
									agents[i].agent_id, agents[i].name,
									agents[i].core_skills);
						}
						agent_turn_queue_outbound_text(&msg, resp, NULL, true);
					}
				} else if (strncmp(cmd, "hr agents", 9) == 0 ||
				           (is_hr && strncmp(cmd, "agents", 6) == 0)) {
					/* JSON 格式供 Web 下拉框 */
					agent_definition_t agents[AGENT_PROFILES_MAX];
					int count = 0;
					agent_registry_list(false, agents, AGENT_PROFILES_MAX, &count);
					char *resp = kmalloc(4096, GFP_KERNEL);
					if (resp) {
						int off = snprintf(resp, 4096,
							"{\"agents\":[{\"id\":\"boss\",\"name\":\"Boss\"},{\"id\":\"hr\",\"name\":\"HR\"}");
						for (int i = 0; i < count && off < 3900; i++) {
							off += snprintf(resp + off, (size_t)(4096 - off),
									",{\"id\":\"%s\",\"name\":\"%s\"}",
									agents[i].agent_id, agents[i].name);
						}
						off += snprintf(resp + off, (size_t)(4096 - off), "]}");
						agent_turn_queue_outbound_text(&msg, resp, NULL, true);
					}
				} else if (!is_hr) {
					char *resp = strdup("Unknown HR command.");
					agent_turn_queue_outbound_text(&msg, resp, NULL, true);
				} else {
					/* HR 模式下非命令消息：转发到正常 pipeline */
					kfree(msg.content);
					msg.content = NULL;
					kfree(msg.reasoning);
					msg.reasoning = NULL;
					kfree(msg.image_path);
					msg.image_path = NULL;
					agent_turn_process_new_message(&msg);
					continue;
				}
				kfree(msg.content);
				msg.content = NULL;
				kfree(msg.reasoning);
				msg.reasoning = NULL;
				kfree(msg.image_path);
				msg.image_path = NULL;
			} else {
				agent_turn_process_new_message(&msg);
			}
		} else {
			/* 无新消息也无回复，稍微休眠 */
			usleep(50000);  /* 50ms */
		}

		/* 3. HR 自动触发检查 */
		tick++;
		if (tick >= HR_AUTO_CHECK_INTERVAL_TICKS) {
			tick = 0;
			time_t now = time(NULL);
			if (now - last_hr_run >= HR_AUTO_MIN_INTERVAL_SEC) {
				/* 查询上次 HR 运行后的新 transcript */
				transcript_record_t records[HR_AUTO_MIN_NEW_COUNT];
				int count = 0;
				err_t qerr = transcript_query(NULL, "success", last_hr_run,
				                             HR_AUTO_MIN_NEW_COUNT,
				                             records, HR_AUTO_MIN_NEW_COUNT,
				                             &count);
				if (qerr == 0 && count >= HR_AUTO_MIN_NEW_COUNT) {
					pr_info("HR auto-trigger: %d new successful transcripts since last run",
						count);
					int registered = 0;
					hr_run_pipeline(true, &registered);
					last_hr_run = now;
					if (registered > 0) {
						pr_info("HR auto: %d new specialist(s) registered",
							registered);
					}
				}
			}
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
	delegate_parent_wake_init();
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
