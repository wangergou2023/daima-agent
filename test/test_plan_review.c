#include "plan.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static void test_implement_intent_generates_reviewed_plan(void)
{
    struct plan plan = {0};
    int err = plan_review_generate(
        INTENT_IMPLEMENT,
        "实现 PlanReview 流水线",
        "base system prompt",
        &plan);

    assert(!err);
    assert(plan.has_plan);
    assert(plan.reviewed);
    assert(strstr(plan.plan_text, "## 执行计划") != NULL);
    assert(strstr(plan.plan_text, "1.") != NULL);
}

static void test_fix_intent_generates_reviewed_plan(void)
{
    struct plan plan = {0};
    int err = plan_review_generate(
        INTENT_FIX,
        "修复构建错误",
        NULL,
        &plan);

    assert(!err);
    assert(plan.has_plan);
    assert(plan.reviewed);
    assert(strstr(plan.plan_text, "修复构建错误") != NULL);
}

static void test_qa_and_open_skip_planning(void)
{
    struct plan plan = {0};

    assert(plan_review_generate(INTENT_QA, "解释一下", NULL, &plan) == 0);
    assert(!plan.has_plan);
    assert(!plan.reviewed);
    assert(plan.plan_text[0] == '\0');

    assert(plan_review_generate(INTENT_OPEN, "随便聊聊", NULL, &plan) == 0);
    assert(!plan.has_plan);
    assert(!plan.reviewed);
    assert(plan.plan_text[0] == '\0');
}

static void test_inject_appends_only_reviewed_plan(void)
{
    char prompt[8192] = "原始系统提示";
    struct plan plan = {
        .plan_text = "## 执行计划\n1. 读取代码\n2. 修改代码\n",
        .has_plan = true,
        .reviewed = true,
    };

    assert(plan_review_inject_to_prompt(&plan, prompt, sizeof(prompt)) == 0);
    assert(strstr(prompt, "原始系统提示") != NULL);
    assert(strstr(prompt, "以下是预先生成的执行计划") != NULL);
    assert(strstr(prompt, "1. 读取代码") != NULL);
    assert(strstr(prompt, "完成每一步后") != NULL);
}

static void test_inject_ignores_missing_or_unreviewed_plan(void)
{
    char prompt[256] = "原始系统提示";
    struct plan plan = {
        .plan_text = "## 执行计划\n1. 不应注入\n",
        .has_plan = true,
        .reviewed = false,
    };

    assert(plan_review_inject_to_prompt(&plan, prompt, sizeof(prompt)) == 0);
    assert(strcmp(prompt, "原始系统提示") == 0);
}

int main(void)
{
    test_implement_intent_generates_reviewed_plan();
    test_fix_intent_generates_reviewed_plan();
    test_qa_and_open_skip_planning();
    test_inject_appends_only_reviewed_plan();
    test_inject_ignores_missing_or_unreviewed_plan();
    printf("test_plan_review: OK\n");
    return 0;
}
