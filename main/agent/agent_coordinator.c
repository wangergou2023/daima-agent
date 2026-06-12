#include "agent/agent_coordinator.h"

#include <stdio.h>
#include <string.h>

static int coordinator_roles_for_intent(daima_intent_t intent,
                                        agent_role_t roles[COORDINATOR_MAX_SUB_AGENTS])
{
    if (!roles) {
        return 0;
    }

    switch (intent) {
    case DAIMA_INTENT_IMPLEMENT:
        roles[0] = AGENT_ROLE_PLANNER;
        roles[1] = AGENT_ROLE_EXECUTOR;
        roles[2] = AGENT_ROLE_REVIEWER;
        return 3;
    case DAIMA_INTENT_FIX:
        roles[0] = AGENT_ROLE_EXECUTOR;
        roles[1] = AGENT_ROLE_REVIEWER;
        return 2;
    case DAIMA_INTENT_QA:
    case DAIMA_INTENT_OPEN:
    case DAIMA_INTENT_INVESTIGATE:
        roles[0] = AGENT_ROLE_FAST;
        return 1;
    case DAIMA_INTENT_COUNT:
    default:
        return 0;
    }
}

static void coordinator_set_task_description(sub_agent_t *agent,
                                             const daima_plan_t *plan,
                                             const char *user_message)
{
    const char *role_name = agent_role_name(agent->role);
    const char *message = user_message ? user_message : "";
    const char *plan_text = (plan && plan->has_plan) ? plan->plan_text : "";
    size_t used = 0;

    used = (size_t)snprintf(agent->task_description,
                            sizeof(agent->task_description),
                            "[%s] 用户任务：",
                            role_name);
    if (used >= sizeof(agent->task_description)) {
        return;
    }

    strncat(agent->task_description,
            message,
            sizeof(agent->task_description) - used - 1);
    used = strlen(agent->task_description);

    if (plan_text[0] != '\0' && used < sizeof(agent->task_description) - 1) {
        strncat(agent->task_description,
                "\n计划：",
                sizeof(agent->task_description) - used - 1);
        used = strlen(agent->task_description);
        strncat(agent->task_description,
                plan_text,
                sizeof(agent->task_description) - used - 1);
    }
}

daima_err_t coordinator_decompose(daima_intent_t intent,
                                   const daima_plan_t *plan,
                                   const char *user_message,
                                   coordinator_t *out)
{
    if (!out) {
        return DAIMA_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));

    agent_role_t roles[COORDINATOR_MAX_SUB_AGENTS] = {0};
    int count = coordinator_roles_for_intent(intent, roles);
    if (count <= 0 || count > COORDINATOR_MAX_SUB_AGENTS) {
        return DAIMA_ERR_INVALID_ARG;
    }

    out->agent_count = count;
    for (int i = 0; i < count; i++) {
        sub_agent_t *agent = &out->agents[i];
        agent->role = roles[i];
        snprintf(agent->system_prompt_add,
                 sizeof(agent->system_prompt_add),
                 "%s",
                 agent_role_prompt_suffix(agent->role));
        coordinator_set_task_description(agent, plan, user_message);
        agent->done = false;
        agent->error = DAIMA_OK;
    }

    return DAIMA_OK;
}

daima_err_t coordinator_merge_results(coordinator_t *coord,
                                       char *output, size_t output_size)
{
    if (!coord || !output || output_size == 0) {
        return DAIMA_ERR_INVALID_ARG;
    }

    output[0] = '\0';
    coord->merged_result[0] = '\0';

    size_t used = 0;
    for (int i = 0; i < coord->agent_count && i < COORDINATOR_MAX_SUB_AGENTS; i++) {
        const sub_agent_t *agent = &coord->agents[i];
        if (!agent->done) {
            continue;
        }

        int written = snprintf(output + used,
                               output_size - used,
                               "## %s\n%s\n",
                               agent_role_name(agent->role),
                               agent->result_text);
        if (written < 0) {
            return DAIMA_FAIL;
        }
        if ((size_t)written >= output_size - used) {
            used = output_size - 1;
            break;
        }
        used += (size_t)written;
    }

    snprintf(coord->merged_result, sizeof(coord->merged_result), "%s", output);
    return DAIMA_OK;
}

void coordinator_free(coordinator_t *coord)
{
    if (!coord) {
        return;
    }
    memset(coord, 0, sizeof(*coord));
}
