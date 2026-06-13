#include "plan.h"

#include <stdio.h>
#include <string.h>

static bool plan_review_intent_requires_plan(daima_intent_t intent)
{
    return intent == DAIMA_INTENT_IMPLEMENT || intent == DAIMA_INTENT_FIX;
}

static bool plan_review_has_numbered_step(const char *text)
{
    if (!text) {
        return false;
    }

    for (const char *p = text; *p; p++) {
        if ((p == text || p[-1] == '\n') && p[0] >= '1' && p[0] <= '9' && p[1] == '.') {
            return true;
        }
    }
    return false;
}

static bool plan_review_is_placeholder_only(const char *text)
{
    if (!text || !text[0]) {
        return true;
    }

    return strstr(text, "[步骤1描述]") != NULL ||
           strstr(text, "TODO") != NULL ||
           strstr(text, "TBD") != NULL;
}

daima_err_t plan_review_generate(daima_intent_t intent,
                                  const char *user_message,
                                  const char *system_prompt,
                                  daima_plan_t *out_plan)
{
    (void)system_prompt;

    if (!out_plan) {
        return DAIMA_ERR_INVALID_ARG;
    }

    memset(out_plan, 0, sizeof(*out_plan));

    if (!plan_review_intent_requires_plan(intent)) {
        return DAIMA_OK;
    }

    const char *task = (user_message && user_message[0]) ? user_message : "当前用户任务";
    int n = snprintf(out_plan->plan_text,
                     sizeof(out_plan->plan_text),
                     "## 执行计划\n"
                     "1. 理解用户任务与现有上下文，确认要完成的目标：%s\n"
                     "2. 检查相关代码、配置或测试，定位需要修改的位置。\n"
                     "3. 按现有项目约定实现最小必要改动，并避免影响无关流程。\n"
                     "4. 运行针对性验证和项目要求的构建/测试，确认结果符合预期。\n",
                     task);

    if (n < 0) {
        out_plan->plan_text[0] = '\0';
        return DAIMA_OK;
    }
    out_plan->plan_text[sizeof(out_plan->plan_text) - 1] = '\0';

    if (plan_review_is_placeholder_only(out_plan->plan_text) ||
        !plan_review_has_numbered_step(out_plan->plan_text)) {
        out_plan->plan_text[0] = '\0';
        return DAIMA_OK;
    }

    out_plan->has_plan = true;
    out_plan->reviewed = true;
    return DAIMA_OK;
}

daima_err_t plan_review_inject_to_prompt(const daima_plan_t *plan,
                                          char *system_prompt,
                                          size_t system_prompt_size)
{
    if (!system_prompt || system_prompt_size == 0) {
        return DAIMA_ERR_INVALID_ARG;
    }

    if (!plan || !plan->has_plan || !plan->reviewed || !plan->plan_text[0]) {
        return DAIMA_OK;
    }

    size_t off = strnlen(system_prompt, system_prompt_size - 1);
    if (off >= system_prompt_size - 1) {
        return DAIMA_OK;
    }

    int n = snprintf(system_prompt + off,
                     system_prompt_size - off,
                     "\n\n## 执行计划\n"
                     "以下是预先生成的执行计划，请按照此计划逐步完成任务:\n\n"
                     "%s\n"
                     "完成每一步后，请确认该步骤已完成再继续下一步。\n",
                     plan->plan_text);
    if (n < 0 || (size_t)n >= (system_prompt_size - off)) {
        system_prompt[system_prompt_size - 1] = '\0';
    }

    return DAIMA_OK;
}
