#include "sched.h"
#include "autoconf.h"
#include "linux/printk.h"
#include "drivers/llm/llm_proxy.h"
#include "linux/slab.h"
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

    char safe[2048];
    if (prompt) {
        const char *s = prompt; char *d = safe;
        while (*s && (size_t)(d - safe) < sizeof(safe) - 1) {
            char ch = *s++;
            *d++ = (ch == '\n' || ch == '\r') ? ' ' : ch;
        }
        *d = '\0';
    } else {
        safe[0] = '\0';
    }

    llm_response_t resp;
    memset(&resp, 0, sizeof(resp));
    agent->error = llm_chat_tools_with_model(safe[0] ? safe : "ok", messages, tools,
                                              llm_get_model_name(), &resp);
    if (agent->error == 0 && resp.text) {
        strscpy(agent->result, resp.text, sizeof(agent->result));
        agent->state = SCHED_AGENT_DONE;
    } else {
        agent->state = SCHED_AGENT_ERROR;
    }
    llm_response_free(&resp);
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
