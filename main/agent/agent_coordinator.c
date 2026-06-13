#include "agent/agent_coordinator.h"
#include "daima_config.h"
#include "daima_log.h"
#include "daima_os.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "coordinator";

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

#define COORDINATOR_DISPLAY_MAX 2000

    size_t used = 0;

    for (int i = 0; i < coord->agent_count && i < COORDINATOR_MAX_SUB_AGENTS; i++) {
        const sub_agent_t *agent = &coord->agents[i];
        if (!agent->done) continue;
        const char *icon = agent->error == DAIMA_OK ? "✅" : "❌";
        used += snprintf(output + used, output_size - used,
                        "%s %s %s\n",
                        icon,
                        agent_role_name(agent->role),
                        agent->error == DAIMA_OK ? "已完成" :
                        agent->error == DAIMA_ERR_TIMEOUT ? "超时" : "失败");
    }

    for (int i = 0; i < coord->agent_count && i < COORDINATOR_MAX_SUB_AGENTS; i++) {
        const sub_agent_t *agent = &coord->agents[i];
        if (agent->done) continue;
        used += snprintf(output + used, output_size - used,
                        "⏱️ %s 未完成 (超时)\n",
                        agent_role_name(agent->role));
    }

    used += snprintf(output + used, output_size - used, "\n---\n\n");

    const sub_agent_t *executor = NULL;
    const sub_agent_t *reviewer = NULL;

    for (int i = 0; i < coord->agent_count && i < COORDINATOR_MAX_SUB_AGENTS; i++) {
        const sub_agent_t *agent = &coord->agents[i];
        if (!agent->done) continue;
        if (agent->role == AGENT_ROLE_EXECUTOR) executor = agent;
        if (agent->role == AGENT_ROLE_REVIEWER) reviewer = agent;
    }

    if (executor && executor->result_text[0]) {
        size_t elen = strlen(executor->result_text);
        if (elen > COORDINATOR_DISPLAY_MAX) {
            int written = snprintf(output + used, output_size - used,
                     "%.2000s\n\n...(输出过长已截断, 查看文件获取完整内容)", executor->result_text);
            if (written < 0) return DAIMA_FAIL;
            used += (size_t)written;
        } else {
            int written = snprintf(output + used, output_size - used, "%s", executor->result_text);
            if (written < 0) return DAIMA_FAIL;
            used += (size_t)written;
        }
    }

    if (reviewer && reviewer->result_text[0] &&
        strstr(reviewer->result_text, "审查通过") == NULL &&
        strstr(reviewer->result_text, "已完成") == NULL) {
        snprintf(output + used, output_size - used,
                 "\n\n---\n⚠️ REVIEWER: %s", reviewer->result_text);
    }

    snprintf(coord->merged_result, sizeof(coord->merged_result), "%s", output);
    return DAIMA_OK;
}

daima_err_t coordinator_launch_all(const char *base_system_prompt,
                                   cJSON *shared_messages,
                                   const char *tools_json,
                                   coordinator_t *coord)
{
    if (!coord || coord->agent_count <= 1) {
        return DAIMA_OK;
    }

    const char *base_prompt = base_system_prompt ? base_system_prompt : "";

    for (int i = 0; i < coord->agent_count && i < COORDINATOR_MAX_SUB_AGENTS; i++) {
        sub_agent_t *agent = &coord->agents[i];
        char scoped_prompt[DAIMA_CONTEXT_BUF_SIZE];

        int written = snprintf(scoped_prompt,
                               sizeof(scoped_prompt),
                               "%s\n%s\n\n## 子任务\n%s",
                               base_prompt,
                               agent->system_prompt_add,
                               agent->task_description);
        if (written < 0 || (size_t)written >= sizeof(scoped_prompt)) {
            scoped_prompt[sizeof(scoped_prompt) - 1] = '\0';
        }

        agent->scoped_messages = shared_messages ? cJSON_Duplicate(shared_messages, 1) : cJSON_CreateArray();
        if (!agent->scoped_messages) {
            agent->error = DAIMA_FAIL;
            agent->done = true;
            DAIMA_LOGW(TAG, "Coordinator: failed to copy messages for sub-agent %d (%s)",
                       i, agent_role_name(agent->role));
            continue;
        }

        agent->async_chat = llm_chat_tools_async(scoped_prompt,
                                                 agent->scoped_messages,
                                                 tools_json,
                                                 NULL);
        if (!agent->async_chat) {
            agent->error = DAIMA_FAIL;
            agent->done = true;
            DAIMA_LOGW(TAG, "Coordinator: failed to launch sub-agent %d (%s)",
                       i, agent_role_name(agent->role));
        } else {
            agent->error = DAIMA_OK;
            agent->done = false;
            DAIMA_LOGI(TAG, "Coordinator: launched sub-agent %d (%s)",
                       i, agent_role_name(agent->role));
        }
    }

    return DAIMA_OK;
}

daima_err_t coordinator_wait_all(coordinator_t *coord, int timeout_ms)
{
    if (!coord) {
        return DAIMA_ERR_INVALID_ARG;
    }

    int pending = 0;
    for (int i = 0; i < coord->agent_count && i < COORDINATOR_MAX_SUB_AGENTS; i++) {
        if (!coord->agents[i].done) {
            pending++;
        }
    }

    int elapsed = 0;
    const int poll_interval_ms = 100;
    int last_progress_sec = 0;
    while (pending > 0 && elapsed < timeout_ms) {
        for (int i = 0; i < coord->agent_count && i < COORDINATOR_MAX_SUB_AGENTS; i++) {
            sub_agent_t *agent = &coord->agents[i];
            if (agent->done) {
                continue;
            }

            if (llm_chat_async_is_done(agent->async_chat)) {
                llm_response_t resp = {0};
                agent->error = llm_chat_async_get_response(agent->async_chat, &resp);
                if (agent->error == DAIMA_OK && resp.text) {
                    snprintf(agent->result_text, sizeof(agent->result_text), "%s", resp.text);
                }
                llm_response_free(&resp);
                agent->done = true;
                pending--;
                DAIMA_LOGI(TAG, "Coordinator: sub-agent %d (%s) done, err=%s",
                           i, agent_role_name(agent->role), daima_err_to_name(agent->error));
            }
        }

        if (pending > 0) {
            daima_task_delay((uint32_t)poll_interval_ms);
            elapsed += poll_interval_ms;
            int elapsed_sec = elapsed / 1000;
            if (elapsed_sec > 0 && elapsed_sec % 10 == 0 && elapsed_sec != last_progress_sec) {
                last_progress_sec = elapsed_sec;
                DAIMA_LOGI(TAG, "Coordinator: waiting... %ds elapsed, %d sub-agents pending",
                           elapsed_sec, pending);
            }
        }
    }

    for (int i = 0; i < coord->agent_count && i < COORDINATOR_MAX_SUB_AGENTS; i++) {
        sub_agent_t *agent = &coord->agents[i];
        if (!agent->done) {
            agent->error = DAIMA_ERR_TIMEOUT;
            agent->done = true;
            DAIMA_LOGW(TAG, "Coordinator: sub-agent %d (%s) timed out",
                       i, agent_role_name(agent->role));
        }
    }

    return DAIMA_OK;
}

void coordinator_free(coordinator_t *coord)
{
    if (!coord) {
        return;
    }
    for (int i = 0; i < coord->agent_count && i < COORDINATOR_MAX_SUB_AGENTS; i++) {
        if (coord->agents[i].async_chat) {
            llm_chat_async_free(coord->agents[i].async_chat);
            coord->agents[i].async_chat = NULL;
        }
        if (coord->agents[i].scoped_messages) {
            cJSON_Delete(coord->agents[i].scoped_messages);
            coord->agents[i].scoped_messages = NULL;
        }
    }
    memset(coord, 0, sizeof(*coord));
}
