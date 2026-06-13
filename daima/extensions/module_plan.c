#include "hooks.h"
#include "state.h"
#include "plan.h"
#include "autoconf.h"
#include "linux/module.h"
#include "linux/printk.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("daima");
MODULE_DESCRIPTION("Agent Extension: plan_review");

static const char *TAG = "ext_plan_review";

static daima_err_t on_intent(daima_msg_t *msg)
{
#if AGENT_EXTENSIONS_ENABLED
    if (msg->intent == DAIMA_INTENT_IMPLEMENT || msg->intent == DAIMA_INTENT_FIX) {
        daima_plan_t *plan = agent_extension_state_plan();
        daima_err_t err = plan_review_generate(msg->intent, msg->content, "", plan);
        if (err == DAIMA_OK && plan->has_plan && plan->reviewed) {
            DAIMA_LOGI(TAG, "Plan generated and reviewed for intent=%s", daima_intent_name(msg->intent));
        }
    }
#endif
    return DAIMA_OK;
}

static daima_err_t on_prepare(daima_msg_t *msg, char *system_prompt,
                              size_t system_prompt_size, cJSON *messages)
{
    (void)msg;
    (void)messages;
#if AGENT_EXTENSIONS_ENABLED
    return plan_review_inject_to_prompt(agent_extension_state_plan(), system_prompt, system_prompt_size);
#else
    return DAIMA_OK;
#endif
}

static agent_extension_hooks_t ext = {
    .name = "plan_review",
    .on_intent = on_intent,
    .on_prepare = on_prepare,
    .enabled = true,
};

__attribute__((constructor)) static void register_ext(void)
{
    agent_hooks_register(&ext);
}
