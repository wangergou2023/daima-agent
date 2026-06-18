/* 调度协调模块：replace_run 钩子中启动多 Agent 并行调度（PLANNER+EXECUTOR+REVIEWER），合并输出。 */

#include "hooks.h"
#include "kernel/sched/sched.h"
#include "turn_persist.h"
#include "state.h"
#include "runtime.h"
#include "linux/module.h"
#include "linux/printk.h"
#include "drivers/platform/platform.h"

#include <stdlib.h>
#include <string.h>
#include "linux/slab.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("agent");
MODULE_DESCRIPTION("Agent Extension: coordinator");

/**
 * replace_run 钩子：按意图启动多 Agent 并行调度。
 * 流程：sched_dispatch → sched_start → sched_wait → sched_merge。
 * 单 Agent 时（nr_agents <= 1）返回 ERR_FAIL 让钩子链继续。
 * @param msg             入站消息
 * @param system_prompt   system prompt
 * @param messages        JSON 消息数组
 * @param tools_json      工具 JSON
 * @param out_final_text  输出：合并后的最终文本
 * @return 成功返回 0，失败返回 ERR_FAIL
 */
static err_t replace_run(struct message *msg, char *system_prompt,
                               cJSON *messages, const char *tools_json,
                               char **out_final_text)
{
    struct sched_runqueue rq;
    memset(&rq, 0, sizeof(rq));
    err_t coord_err = sched_dispatch(msg->intent,
                                           agent_extension_state_plan(),
                                           msg->content,
                                           &rq);
    if (coord_err != 0) {
        pr_warn("Coordinator skipped: %s", err_name(coord_err));
        sched_exit(&rq);
        return ERR_FAIL;
    }
    if (rq.nr_agents <= 1) {
        sched_exit(&rq);
        return ERR_FAIL;
    }

    /* 通知用户并行调度信息 */
    pr_info("Coordinator: launching %d sub-agents for intent=%s", rq.nr_agents, intent_name(msg->intent));
    char thinking_msg[512];
    int off = snprintf(thinking_msg, sizeof(thinking_msg),
                       "🤖 Coordinator 并行处理中 (%d个子Agent", rq.nr_agents);
    for (int i = 0; i < rq.nr_agents && i < SCHED_MAX_AGENTS; i++) {
        off += snprintf(thinking_msg + off, sizeof(thinking_msg) - (size_t)off,
                        "%s%s", i == 0 ? ": " : " + ", sched_class_name(rq.agents[i].class));
    }
    snprintf(thinking_msg + off, sizeof(thinking_msg) - (size_t)off, ")");
    agent_turn_queue_outbound_text(msg, strdup(thinking_msg), NULL, true);

    /* 启动并行调度，超时比 LLM 请求多 10 秒 */
    rq.timeout_ms = runtime_config_get_request_timeout_ms() + 10000;
    sched_start(&rq, system_prompt, messages, tools_json);
    err_t err = sched_wait(&rq);
    if (err == 0) {
        char *merged = platform_calloc(1, SCHED_MERGED_MAX);
        if (!merged) {
            sched_exit(&rq);
            return ERR_NO_MEM;
        }
        sched_merge(&rq, merged, SCHED_MERGED_MAX);
        if (merged[0] != '\0') {
            *out_final_text = merged;
            merged = NULL;
        }
        kfree(merged);
    } else {
        pr_warn("Coordinator launch skipped: %s", err_name(err));
    }
    sched_exit(&rq);
    return 0;
}

static agent_extension_hooks_t ext = {
    .name = "coordinator",
    .replace_run = replace_run,
    .enabled = false,   /* 改由 LLM 通过 delegate_task 工具自主决定 */
};

static int __init sched_module_init(void)
{
    agent_hooks_register(&ext);
    return 0;
}

static void __exit sched_module_exit(void)
{
}

module_init(sched_module_init);
module_exit(sched_module_exit);
