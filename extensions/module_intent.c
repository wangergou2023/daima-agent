/* 意图分类模块：在 on_intent 钩子中调用 intent_gate_classify，将用户消息分类为 QA/IMPLEMENT/INVESTIGATE/FIX/OPEN。 */

#include "hooks.h"
#include "intent.h"
#include "autoconf.h"
#include "linux/module.h"
#include "linux/printk.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("agent");
MODULE_DESCRIPTION("Agent Extension: intent_gate");

/**
 * intent 钩子：对每条入站消息进行意图分类。
 * @param msg 入站消息指针，分类结果写入 msg->intent
 * @return 始终返回 0
 */
static err_t on_intent(struct message *msg)
{
#if AGENT_EXTENSIONS_ENABLED
    intent_gate_classify(msg->content, &msg->intent);
    pr_info("Intent classified: %s -> %s", msg->content, intent_name(msg->intent));
#endif
    return 0;
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
