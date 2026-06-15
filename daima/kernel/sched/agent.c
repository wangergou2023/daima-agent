#include "sched.h"
#include "autoconf.h"
#include "linux/printk.h"

#include <stdio.h>
#include <string.h>
#include "linux/kernel.h"
void sched_agent_init(struct sched_agent *agent, const struct sched_class *cls,
                      const char *task)
{
    if (!agent || !cls) {
        return;
    }

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
    if (!agent || agent->state != SCHED_AGENT_WAITING) {
        return;
    }

    agent->scoped_messages = messages ? cJSON_Duplicate(messages, 1) : cJSON_CreateArray();
    if (!agent->scoped_messages) {
        agent->error = ERR_FAIL;
        agent->state = SCHED_AGENT_ERROR;
        pr_warn("failed to copy messages for agent %d (%s)", agent->pid, sched_class_name(agent->class));
        return;
    }

    agent->async_chat = llm_chat_tools_async(prompt ? prompt : "",
                                             agent->scoped_messages,
                                             tools,
                                             NULL);
    if (!agent->async_chat) {
        agent->error = ERR_FAIL;
        agent->state = SCHED_AGENT_ERROR;
        pr_warn("failed to launch agent %d (%s)", agent->pid, sched_class_name(agent->class));
        return;
    }

    agent->state = SCHED_AGENT_RUNNING;
    agent->error = 0;
    pr_info("launched agent %d (%s)", agent->pid, sched_class_name(agent->class));
}

bool sched_agent_is_done(struct sched_agent *agent)
{
    if (!agent || agent->state != SCHED_AGENT_RUNNING) {
        return false;
    }
    return llm_chat_async_is_done(agent->async_chat);
}

void sched_agent_reap(struct sched_agent *agent)
{
    if (!agent || agent->state != SCHED_AGENT_RUNNING) {
        return;
    }

    llm_response_t resp = {0};
    agent->error = llm_chat_async_get_response(agent->async_chat, &resp);
    if (agent->error == 0 && resp.text) {
        strscpy(agent->result, resp.text, sizeof(agent->result));
        agent->state = SCHED_AGENT_DONE;
    } else {
        agent->state = SCHED_AGENT_ERROR;
    }
    llm_response_free(&resp);
}
