/* delegate_task 工具：LLM 调用它来派发子 Agent 并行工作 */
#include "drivers/tool/tool_registry.h"
#include "kernel/sched/sched.h"
#include "plan.h"
#include "turn_common.h"
#include "runtime.h"
#include "linux/printk.h"
#include "linux/slab.h"
#include "cjson.h"
#include <stdio.h>
#include <string.h>

#define MERGED_MAX (64 * 1024)

static err_t delegate_task_execute(const char *input_json,
                                    char *output, size_t output_size)
{
    cJSON *input = cJSON_Parse(input_json);
    const char *task = cJSON_GetStringValue(cJSON_GetObjectItem(input, "task"));
    const char *intent_str = cJSON_GetStringValue(cJSON_GetObjectItem(input, "intent"));
    int agent_count = 0;
    cJSON *count_j = cJSON_GetObjectItem(input, "agent_count");
    if (count_j && cJSON_IsNumber(count_j)) agent_count = count_j->valueint;

    cJSON_Delete(input);

    if (!task || !task[0]) {
        snprintf(output, output_size, "delegate_task: missing 'task' field");
        return ERR_INVALID_ARG;
    }

    enum intent it = INTENT_IMPLEMENT;
    if (intent_str && strcmp(intent_str, "FIX") == 0) it = INTENT_FIX;

    struct plan p = { .has_plan = true, .reviewed = true, .plan_text = (char *)task };

    struct sched_runqueue rq;
    memset(&rq, 0, sizeof(rq));
    err_t err = sched_dispatch(it, &p, task, &rq);
    if (err != 0 || rq.nr_agents <= 0) {
        sched_exit(&rq);
        snprintf(output, output_size, "delegate_task: dispatch failed, %d agents", rq.nr_agents);
        return ERR_FAIL;
    }

    /* 限制 agent 数量 */
    if (agent_count > 0 && agent_count < rq.nr_agents)
        rq.nr_agents = agent_count;

    pr_info("delegate_task: launching %d sub-agents for '%s'", rq.nr_agents, task);
    rq.timeout_ms = runtime_config_get_request_timeout_ms() + 10000;

    char *system_prompt = NULL;
    cJSON *messages = cJSON_CreateArray();
    char *tools = NULL;

    sched_start(&rq, system_prompt, messages, tools);
    err = sched_wait(&rq);
    if (err != 0) {
        sched_exit(&rq);
        cJSON_Delete(messages);
        snprintf(output, output_size, "delegate_task: wait failed: %s", err_name(err));
        return ERR_FAIL;
    }

    char *merged = kzalloc(MERGED_MAX, GFP_KERNEL);
    if (!merged) {
        sched_exit(&rq);
        cJSON_Delete(messages);
        return ERR_NO_MEM;
    }
    sched_merge(&rq, merged, MERGED_MAX);
    sched_exit(&rq);
    cJSON_Delete(messages);

    if (merged[0]) {
        strscpy(output, merged, output_size);
    } else {
        snprintf(output, output_size, "delegate_task: no output from %d agents", rq.nr_agents);
    }
    kfree(merged);
    return 0;
}

static struct tool s_delegate_task = {
    .name = "delegate_task",
    .description = "将复杂任务拆解为多个子 Agent 并行处理。"
                   "PARALLEL IS DEFAULT — 并行是默认模式，串行是例外。"
                   "当你需要分工协作时，不要自己做——分解任务，一次性分发给 PLANNER+EXECUTOR+REVIEWER，让它们并行工作。"
                   "子 Agent 会独立执行并返回结果，你无需等待中间过程。"
                   "参数: task(任务描述), intent(IMPLEMENT或FIX), agent_count(可选)",
    .input_schema_json =
        "{\"type\":\"object\","
        "\"properties\":{"
        "\"task\":{\"type\":\"string\",\"description\":\"任务描述\"},"
        "\"intent\":{\"type\":\"string\",\"description\":\"IMPLEMENT 或 FIX，默认 IMPLEMENT\"},"
        "\"agent_count\":{\"type\":\"integer\",\"description\":\"可选，限制子Agent数量\"}"
        "},"
        "\"required\":[\"task\"]}",
    .execute = delegate_task_execute,
};

const struct tool *tool_delegate_definition(void)
{
    return &s_delegate_task;
}
