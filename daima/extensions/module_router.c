#include "hooks.h"
#include "router.h"
#include "autoconf.h"
#include "linux/module.h"
#include "linux/printk.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("daima");
MODULE_DESCRIPTION("Agent Extension: category_router");
static err_t before_run(struct message *msg, const char **model_override,
                              const char *tools_json)
{
    (void)tools_json;
#if AGENT_EXTENSIONS_ENABLED
    category_router_cfg_t cfg = category_router_load_and_get_cfg();
    if (cfg.enabled) {
        const daima_category_profile_t *profile = category_router_resolve(msg->intent);
        if (profile) {
            *model_override = profile->model;
            pr_info("Category routing: intent=%s -> model=%s", daima_intent_name(msg->intent), profile->model);
        }
    }
#endif
    return 0;
}

static agent_extension_hooks_t ext = {
    .name = "category_router",
    .before_run = before_run,
    .enabled = true,
};

static int __init router_module_init(void)
{
    agent_hooks_register(&ext);
    return 0;
}

static void __exit router_module_exit(void)
{
}

module_init(router_module_init);
module_exit(router_module_exit);
