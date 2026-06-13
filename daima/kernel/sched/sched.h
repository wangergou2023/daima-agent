#pragma once

#include "intent.h"
#include "plan.h"
#include "drivers/llm/llm_proxy.h"
#include "err.h"
#include "linux/list.h"

#include <stdbool.h>
#include <stddef.h>

#include "cJSON.h"

#define SCHED_MAX_AGENTS 4
#define SCHED_CLASSES 3
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
    daima_err_t error;
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

void sched_init(void);
daima_err_t sched_dispatch(daima_intent_t intent, const daima_plan_t *plan,
                           const char *user_msg, struct sched_runqueue *rq);
void sched_start(struct sched_runqueue *rq,
                 const char *system_prompt, cJSON *messages, const char *tools);
daima_err_t sched_wait(struct sched_runqueue *rq);
void sched_merge(struct sched_runqueue *rq, char *output, size_t size);
void sched_exit(struct sched_runqueue *rq);

const struct sched_class *sched_class_for_id(enum sched_class_id id);
const struct sched_class *sched_class_for_intent(daima_intent_t intent, int *count);
const char *sched_class_name(enum sched_class_id id);

void sched_enqueue(struct sched_runqueue *rq, const struct sched_class *cls,
                   const char *task);
void sched_dequeue(struct sched_runqueue *rq, struct sched_agent *agent);
struct sched_agent *sched_pick_next(struct sched_runqueue *rq);
void sched_complete(struct sched_runqueue *rq, struct sched_agent *agent,
                    daima_err_t err);

void sched_agent_init(struct sched_agent *agent, const struct sched_class *cls,
                      const char *task);
void sched_agent_launch(struct sched_agent *agent, const char *prompt,
                        cJSON *messages, const char *tools);
bool sched_agent_is_done(struct sched_agent *agent);
void sched_agent_reap(struct sched_agent *agent);
