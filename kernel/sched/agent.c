#include "sched.h"
#include "autoconf.h"
#include "linux/printk.h"
#include "drivers/llm/llm_proxy.h"
#include "linux/slab.h"
#include "cjson.h"
#include <stdio.h>
#include <string.h>
#include "linux/kernel.h"

void sched_agent_init(struct sched_agent *agent, const struct sched_class *cls,
                      const char *task)
{
    if (!agent || !cls) return;
    memset(agent, 0, sizeof(*agent));
    agent->pid = -1;
    agent->class = (enum sched_class_id)cls->priority;
    agent->state = SCHED_AGENT_WAITING;
    agent->error = 0;
    strscpy(agent->prompt_add, cls->prompt_suffix, sizeof(agent->prompt_add));
    strscpy(agent->task_desc, task ? task : "", sizeof(agent->task_desc));
}

void sched_agent_launch(struct sched_agent *agent, const char *prompt,
                        cJSON *messages, const char *tools)
{
    if (!agent || agent->state != SCHED_AGENT_WAITING) return;
    (void)tools;

    char safe[2048];
    memset(safe, 0, sizeof(safe));
    if (prompt) {
        const char *s = prompt; char *d = safe;
        while (*s && (size_t)(d - safe) < sizeof(safe) - 1) {
            *d++ = (*s == '\n' || *s == '\r') ? ' ' : *s;
            s++;
        }
    }

    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "model", llm_get_model_name());
    cJSON_AddNumberToObject(body, "max_tokens", 16384);
    cJSON_AddStringToObject(body, "system", safe[0] ? safe : "ok");
    cJSON *msgs = messages ? cJSON_Duplicate(messages, 1) : cJSON_CreateArray();
    cJSON_AddItemToObject(body, "messages", msgs);
    char *body_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);

    char output[16384];
    memset(output, 0, sizeof(output));
    agent->error = llm_chat_via_curl(body_str, output, sizeof(output));
    kfree(body_str);

    if (agent->error == 0 && output[0]) {
        strscpy(agent->result, output, sizeof(agent->result));
        agent->state = SCHED_AGENT_DONE;
    } else {
        agent->state = SCHED_AGENT_ERROR;
        agent->error = ERR_FAIL;
    }
    pr_info("agent %d (%s) done, err=%d", agent->pid,
            sched_class_name(agent->class), agent->error);
}

bool sched_agent_is_done(struct sched_agent *agent)
{
    return agent && (agent->state == SCHED_AGENT_DONE || agent->state == SCHED_AGENT_ERROR);
}

void sched_agent_reap(struct sched_agent *agent)
{
    (void)agent;
}
