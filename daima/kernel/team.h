#pragma once

#include "plan.h"
#include "err.h"

#include <stdbool.h>

#define TEAM_MODE_MAX_SUB_AGENTS 3
#define TEAM_MODE_RESULT_MAX 4096
#define TEAM_MODE_DEFAULT_SUB_AGENT_TIMEOUT_MS 30000

typedef struct {
    bool completed;
    char result_text[TEAM_MODE_RESULT_MAX];
    daima_err_t error;
} team_sub_agent_result_t;

typedef struct {
    bool enabled;
    int max_sub_agents;
    int sub_agent_timeout_ms;
    char merged_result[TEAM_MODE_RESULT_MAX * TEAM_MODE_MAX_SUB_AGENTS];
    int completed_count;
} team_orchestrator_t;

daima_err_t team_mode_orchestrate(const daima_plan_t *plan,
                                   const char *system_prompt,
                                   const char *tools_json,
                                   team_orchestrator_t *out);

daima_err_t team_mode_inject_to_prompt(const team_orchestrator_t *team,
                                       char *system_prompt,
                                       size_t system_prompt_size);
