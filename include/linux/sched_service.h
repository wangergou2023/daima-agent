/* 调度器服务接口 —— 驱动层可用的最小 API。
 * 避免 drivers/ 直接依赖 kernel/sched/sched.h 内部细节。 */

#pragma once

#include "err.h"
#include "linux/list.h"
#include "drivers/llm/llm_proxy.h"
#include <stdbool.h>

struct plan;
struct cJSON;

#define SCHED_MAX_AGENTS 4
#define SCHED_RESULT_MAX 4096
#define SCHED_MERGED_MAX 16384

enum sched_class_id {
	SCHED_CLASS_PLANNER = 0,
	SCHED_CLASS_EXECUTOR = 1,
	SCHED_CLASS_REVIEWER = 2,
};

enum sched_agent_state {
	SCHED_AGENT_INIT = 0,
	SCHED_AGENT_WAITING,
	SCHED_AGENT_RUNNING,
	SCHED_AGENT_DONE,
	SCHED_AGENT_TIMEOUT,
	SCHED_AGENT_ERROR,
};

struct sched_class {
	const char *name;
	int priority;
	const char *prompt_suffix;
};

struct sched_agent {
	struct list_head run_list;
	int pid;
	enum sched_class_id class;
	enum sched_agent_state state;
	err_t error;
	char prompt_add[1024];
	char task_desc[512];
	llm_async_chat_t *async_chat;
	llm_response_t response;
	cJSON *scoped_messages;
	char result[SCHED_RESULT_MAX];
	unsigned long start_time;
	unsigned long finish_time;
};

struct sched_runqueue {
	struct list_head agent_list;
	struct sched_agent agents[SCHED_MAX_AGENTS];
	int nr_running;
	int nr_agents;
	int timeout_ms;
	char merged[SCHED_MERGED_MAX];
};

err_t sched_dispatch(int intent, struct plan *plan, const char *task,
		     struct sched_runqueue *rq);

void sched_exit(struct sched_runqueue *rq);

err_t sched_start(struct sched_runqueue *rq, const char *system_prompt,
		  struct cJSON *messages, const char *tools_json);

err_t sched_wait(struct sched_runqueue *rq);

void sched_merge(struct sched_runqueue *rq, char *buf, size_t size);
