#include "agent/team_mode.h"

#include <stdio.h>
#include <string.h>

daima_err_t team_mode_orchestrate(const daima_plan_t *plan,
                                   const char *system_prompt,
                                   const char *tools_json,
                                   team_orchestrator_t *out)
{
    (void)system_prompt;
    (void)tools_json;

    if (!out) {
        return DAIMA_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));
    out->max_sub_agents = TEAM_MODE_MAX_SUB_AGENTS;
    out->sub_agent_timeout_ms = TEAM_MODE_DEFAULT_SUB_AGENT_TIMEOUT_MS;

    if (!plan || !plan->has_plan || !plan->reviewed || plan->plan_text[0] == '\0') {
        return DAIMA_OK;
    }

    int written = snprintf(out->merged_result, sizeof(out->merged_result),
                           "## Team Mode 已启用\n"
                           "计划已生成并通过审查。请按以下步骤执行:\n\n"
                           "%s\n\n"
                           "每完成一个步骤，请明确标注\"✅ 步骤N完成\"。",
                           plan->plan_text);
    if (written < 0) {
        memset(out, 0, sizeof(*out));
        return DAIMA_FAIL;
    }

    out->enabled = true;
    out->completed_count = 1;
    return DAIMA_OK;
}

daima_err_t team_mode_inject_to_prompt(const team_orchestrator_t *team,
                                       char *system_prompt,
                                       size_t system_prompt_size)
{
    if (!team || !system_prompt || system_prompt_size == 0) {
        return DAIMA_ERR_INVALID_ARG;
    }
    if (!team->enabled || team->completed_count <= 0 || team->merged_result[0] == '\0') {
        return DAIMA_OK;
    }

    size_t current_len = strlen(system_prompt);
    if (current_len >= system_prompt_size) {
        return DAIMA_ERR_INVALID_ARG;
    }

    int written = snprintf(system_prompt + current_len,
                           system_prompt_size - current_len,
                           "\n\n%s",
                           team->merged_result);
    if (written < 0 || (size_t)written >= system_prompt_size - current_len) {
        return DAIMA_ERR_NO_MEM;
    }

    return DAIMA_OK;
}
