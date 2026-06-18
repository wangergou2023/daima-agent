/* 计划生成与审查：plan_review_generate() 为 IMPLEMENT/FIX 意图生成执行计划，
 * plan_review_inject_to_prompt() 将计划注入 system prompt。
 * 计划模板包含理解目标→定位代码→最小改动→验证四步，杜绝 TODO/TBD 占位符。 */

#include "plan.h"

#include <stdio.h>
#include <string.h>

/** 判断该意图是否需要生成执行计划。IMPLEMENT 和 FIX 需要，QA/OPEN/INVESTIGATE 不需要。 */
static bool plan_review_intent_requires_plan(enum intent intent)
{
    return intent == INTENT_IMPLEMENT || intent == INTENT_FIX;
}

/** 检查文本是否包含编号步骤（如 "1. xxx"）。 */
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

/** 检查文本是否仅包含占位符（空文本 或 [步骤1描述] / TODO / TBD）。 */
static bool plan_review_is_placeholder_only(const char *text)
{
    if (!text || !text[0]) {
        return true;
    }

    return strstr(text, "[步骤1描述]") != NULL ||
           strstr(text, "TODO") != NULL ||
           strstr(text, "TBD") != NULL;
}

/** 生成执行计划：根据 intent 和用户消息构建四步模板计划，验证无占位符后设置 has_plan=true。
 *  非 IMPLEMENT/FIX 意图直接返回 false，不生成计划。 */
err_t plan_review_generate(enum intent intent,
                                  const char *user_message,
                                  const char *system_prompt,
                                  struct plan *out_plan)
{
    (void)system_prompt;

    if (!out_plan) {
        return ERR_INVALID_ARG;
    }

    memset(out_plan, 0, sizeof(*out_plan));

    if (!plan_review_intent_requires_plan(intent)) {
        return 0;
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
        return 0;
    }
    out_plan->plan_text[sizeof(out_plan->plan_text) - 1] = '\0';

    if (plan_review_is_placeholder_only(out_plan->plan_text) ||
        !plan_review_has_numbered_step(out_plan->plan_text)) {
        out_plan->plan_text[0] = '\0';
        return 0;
    }

    out_plan->has_plan = true;
    out_plan->reviewed = true;
    return 0;
}

/** 将已评审通过的计划注入到 system prompt 末尾，指导 agent 按步骤执行。 */
err_t plan_review_inject_to_prompt(const struct plan *plan,
                                          char *system_prompt,
                                          size_t system_prompt_size)
{
    if (!system_prompt || system_prompt_size == 0) {
        return ERR_INVALID_ARG;
    }

    if (!plan || !plan->has_plan || !plan->reviewed || !plan->plan_text[0]) {
        return 0;
    }

    size_t off = strnlen(system_prompt, system_prompt_size - 1);
    if (off >= system_prompt_size - 1) {
        return 0;
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

    return 0;
}
