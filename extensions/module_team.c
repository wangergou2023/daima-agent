/* 团队模式模块：对有评审计划的 IMPLEMENT/FIX 意图进行多子 Agent 编排（replace_run 钩子）。 */

#include "hooks.h"
#include "state.h"
#include "team.h"
#include "autoconf.h"
#include "linux/module.h"
#include "linux/printk.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("agent");
MODULE_DESCRIPTION("Agent Extension: team_mode");

/**
 * replace_run 钩子：当有计划且已评审时，启动团队模式编排。
 * 编排失败时返回 ERR_FAIL 让钩子链继续。
 * @param msg             入站消息
 * @param system_prompt   system prompt 缓冲区
 * @param messages        JSON 消息数组
 * @param tools_json      工具 JSON
 * @param out_final_text  输出：最终文本
 * @return 编排成功返回 0，否则返回 ERR_FAIL
 */
static err_t replace_run(struct message *msg, char *system_prompt,
                               cJSON *messages, const char *tools_json,
                               char **out_final_text)
{
    (void)msg;
    (void)messages;
    (void)out_final_text;
#if AGENT_EXTENSIONS_ENABLED
    struct plan *plan = agent_extension_state_plan();
    if (plan->has_plan && plan->reviewed) {
        team_orchestrator_t team = {0};
        err_t team_err = team_mode_orchestrate(plan, system_prompt, tools_json, &team);
        if (team_err == 0 && team.completed_count > 0) {
            err_t inject_err = team_mode_inject_to_prompt(&team, system_prompt, CONTEXT_BUF_SIZE);
            if (inject_err == 0) {
                pr_info("Team Mode guidance injected: sub_agents=%d timeout_ms=%d", team.max_sub_agents, team.sub_agent_timeout_ms);
            } else {
                pr_warn("Team Mode prompt injection skipped: %s", err_name(inject_err));
            }
        } else if (team_err != 0) {
            pr_warn("Team Mode orchestration skipped: %s", err_name(team_err));
        }
    }
#endif
    return ERR_FAIL;
}

static agent_extension_hooks_t ext = {
    .name = "team_mode",
    .replace_run = replace_run,
    .enabled = true,
};

static int __init team_module_init(void)
{
    agent_hooks_register(&ext);
    return 0;
}

static void __exit team_module_exit(void)
{
}

module_init(team_module_init);
module_exit(team_module_exit);
