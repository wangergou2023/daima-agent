#include "sched.h"
#include "autoconf.h"
#include "log.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "sched_agent";

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
    agent->error = DAIMA_OK;
    snprintf(agent->prompt_add, sizeof(agent->prompt_add), "%s", cls->prompt_suffix);
    snprintf(agent->task_desc, sizeof(agent->task_desc), "%s", task ? task : "");
}

void sched_agent_launch(struct sched_agent *agent, const char *prompt,
                        cJSON *messages, const char *tools)
{
    if (!agent || agent->state != SCHED_AGENT_WAITING) {
        return;
    }

    agent->scoped_messages = messages ? cJSON_Duplicate(messages, 1) : cJSON_CreateArray();
    if (!agent->scoped_messages) {
        agent->error = DAIMA_FAIL;
        agent->state = SCHED_AGENT_ERROR;
        DAIMA_LOGW(TAG, "failed to copy messages for agent %d (%s)",
                   agent->pid, sched_class_name(agent->class));
        return;
    }

    agent->async_chat = llm_chat_tools_async(prompt ? prompt : "",
                                             agent->scoped_messages,
                                             tools,
                                             NULL);
    if (!agent->async_chat) {
        agent->error = DAIMA_FAIL;
        agent->state = SCHED_AGENT_ERROR;
        DAIMA_LOGW(TAG, "failed to launch agent %d (%s)",
                   agent->pid, sched_class_name(agent->class));
        return;
    }

    agent->state = SCHED_AGENT_RUNNING;
    agent->error = DAIMA_OK;
    DAIMA_LOGI(TAG, "launched agent %d (%s)",
               agent->pid, sched_class_name(agent->class));
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
    if (agent->error == DAIMA_OK && resp.text) {
        snprintf(agent->result, sizeof(agent->result), "%s", resp.text);
        agent->state = SCHED_AGENT_DONE;
    } else {
        agent->state = SCHED_AGENT_ERROR;
    }
    llm_response_free(&resp);
}
