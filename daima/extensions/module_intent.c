#include "hooks.h"
#include "intent.h"
#include "autoconf.h"
#include "linux/module.h"
#include "linux/printk.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("daima");
MODULE_DESCRIPTION("Agent Extension: intent_gate");
static daima_err_t on_intent(daima_msg_t *msg)
{
#if AGENT_EXTENSIONS_ENABLED
    intent_gate_classify(msg->content, &msg->intent);
    pr_info("Intent classified: %s -> %s", msg->content, daima_intent_name(msg->intent));
#endif
    return DAIMA_OK;
}

static agent_extension_hooks_t ext = {
    .name = "intent_gate",
    .on_intent = on_intent,
    .enabled = true,
};

static int __init intent_module_init(void)
{
    agent_hooks_register(&ext);
    return 0;
}

static void __exit intent_module_exit(void)
{
}

module_init(intent_module_init);
module_exit(intent_module_exit);
