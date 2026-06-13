#include "core/hooks.h"
#include "core/state.h"
#include "core/team_mode.h"
#include "core/config.h"
#include "core/log.h"

static const char *TAG = "ext_team_mode";

static daima_err_t replace_run(daima_msg_t *msg, char *system_prompt,
                               cJSON *messages, const char *tools_json,
                               char **out_final_text)
{
    (void)msg;
    (void)messages;
    (void)out_final_text;
#if AGENT_EXTENSIONS_ENABLED
    daima_plan_t *plan = agent_extension_state_plan();
    if (plan->has_plan && plan->reviewed) {
        team_orchestrator_t team = {0};
        daima_err_t team_err = team_mode_orchestrate(plan, system_prompt, tools_json, &team);
        if (team_err == DAIMA_OK && team.completed_count > 0) {
            daima_err_t inject_err = team_mode_inject_to_prompt(&team, system_prompt, DAIMA_CONTEXT_BUF_SIZE);
            if (inject_err == DAIMA_OK) {
                DAIMA_LOGI(TAG, "Team Mode guidance injected: sub_agents=%d timeout_ms=%d",
                           team.max_sub_agents, team.sub_agent_timeout_ms);
            } else {
                DAIMA_LOGW(TAG, "Team Mode prompt injection skipped: %s", daima_err_to_name(inject_err));
            }
        } else if (team_err != DAIMA_OK) {
            DAIMA_LOGW(TAG, "Team Mode orchestration skipped: %s", daima_err_to_name(team_err));
        }
    }
#endif
    return DAIMA_FAIL;
}

static agent_extension_hooks_t ext = {
    .name = "team_mode",
    .replace_run = replace_run,
    .enabled = true,
};

__attribute__((constructor)) static void register_ext(void)
{
    agent_hooks_register(&ext);
}
