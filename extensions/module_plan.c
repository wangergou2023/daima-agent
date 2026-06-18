/* 计划评审模块：对 IMPLEMENT/FIX 意图生成并评审计划，再将评审结果注入 system prompt。 */

#include "hooks.h"
#include "state.h"
#include "plan.h"
#include "autoconf.h"
#include "linux/module.h"
#include "linux/printk.h"

/**
 * intent 钩子：对 IMPLEMENT/FIX 意图调用 LLM 生成计划并评审。
 * Plan 不可含 TODO/TBD 占位符，plan.c 会拒绝并重生成。
 * @param msg 入站消息
 * @return 始终返回 0
 */
static err_t on_intent(struct message *msg)
{
#if AGENT_EXTENSIONS_ENABLED
    if (msg->intent == INTENT_IMPLEMENT || msg->intent == INTENT_FIX) {
        struct plan *plan = agent_extension_state_plan();
        err_t err = plan_review_generate(msg->intent, msg->content, "", plan);
        if (err == 0 && plan->has_plan && plan->reviewed) {
            pr_info("Plan generated and reviewed for intent=%s", intent_name(msg->intent));
        }
    }
#endif
    return 0;
}

/**
 * prepare 钩子：将已评审的计划注入到 system prompt 中。
 * @param msg               入站消息
 * @param system_prompt     system prompt 缓冲区
 * @param system_prompt_size 缓冲区大小
 * @param messages          JSON 消息数组
 * @return 成功返回 0
 */
static err_t on_prepare(struct message *msg, char *system_prompt,
                              size_t system_prompt_size, cJSON *messages)
{
    (void)msg;
    (void)messages;
#if AGENT_EXTENSIONS_ENABLED
    return plan_review_inject_to_prompt(agent_extension_state_plan(), system_prompt, system_prompt_size);
#else
    return 0;
#endif
}

static agent_extension_hooks_t ext = {
    .name = "plan_review",
    .on_intent = on_intent,
    .on_prepare = on_prepare,
    .enabled = true,
};

int __init plan_module_init(void)
{
    agent_hooks_register(&ext);
    return 0;
}

module_init(plan_module_init);
