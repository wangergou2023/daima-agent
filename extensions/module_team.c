#include "hooks.h"
#include "state.h"
#include "team.h"
#include "autoconf.h"
#include "linux/module.h"
#include "linux/printk.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("daima");
MODULE_DESCRIPTION("Agent Extension: team_mode");
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
