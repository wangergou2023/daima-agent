#include "team.h"

#include <assert.h>
#include <errno.h>
#include <string.h>

static void test_null_plan_returns_ok_without_enabling(void)
{
    team_orchestrator_t out;
    memset(&out, 0xA5, sizeof(out));

    int err = team_mode_orchestrate(NULL, "system", "[]", &out);

    assert(!err);
    assert(out.enabled == false);
    assert(!out.completed_count);
    assert(out.merged_result[0] == '\0');
}

static void test_empty_plan_returns_ok_without_enabling(void)
{
    struct plan plan = {0};
    team_orchestrator_t out;
    memset(&out, 0xA5, sizeof(out));

    int err = team_mode_orchestrate(&plan, "system", "[]", &out);

    assert(!err);
    assert(out.enabled == false);
    assert(!out.completed_count);
    assert(out.merged_result[0] == '\0');
}

static void test_unreviewed_plan_returns_ok_without_enabling(void)
{
    struct plan plan = {0};
    plan.has_plan = true;
    plan.reviewed = false;
    strcpy(plan.plan_text, "1. first step");
    team_orchestrator_t out = {0};

    int err = team_mode_orchestrate(&plan, "system", "[]", &out);

    assert(!err);
    assert(out.enabled == false);
    assert(!out.completed_count);
    assert(out.merged_result[0] == '\0');
}

static void test_reviewed_plan_enables_team_mode_and_includes_plan(void)
{
    struct plan plan = {0};
    plan.has_plan = true;
    plan.reviewed = true;
    strcpy(plan.plan_text, "1. Add tests\n2. Implement TeamMode");
    team_orchestrator_t out = {0};

    int err = team_mode_orchestrate(&plan, "system", "[]", &out);

    assert(!err);
    assert(out.enabled == true);
    assert(out.max_sub_agents == TEAM_MODE_MAX_SUB_AGENTS);
    assert(out.sub_agent_timeout_ms > 0);
    assert(out.completed_count == 1);
    assert(strstr(out.merged_result, "Team Mode 已启用") != NULL);
    assert(strstr(out.merged_result, plan.plan_text) != NULL);
    assert(strstr(out.merged_result, "✅ 步骤N完成") != NULL);
}

static void test_null_output_is_invalid_arg(void)
{
    struct plan plan = {0};
    plan.has_plan = true;
    plan.reviewed = true;

    int err = team_mode_orchestrate(&plan, "system", "[]", NULL);

    assert(err == ERR_INVALID_ARG);
}

int main(void)
{
    test_null_plan_returns_ok_without_enabling();
    test_empty_plan_returns_ok_without_enabling();
    test_unreviewed_plan_returns_ok_without_enabling();
    test_reviewed_plan_enables_team_mode_and_includes_plan();
    test_null_output_is_invalid_arg();
    return 0;
}
