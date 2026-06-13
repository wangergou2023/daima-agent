#include "hooks.h"
#include "router.h"
#include "autoconf.h"
#include "log.h"

static const char *TAG = "ext_category_router";

static daima_err_t before_run(daima_msg_t *msg, const char **model_override,
                              const char *tools_json)
{
    (void)tools_json;
#if AGENT_EXTENSIONS_ENABLED
    category_router_cfg_t cfg = category_router_load_and_get_cfg();
    if (cfg.enabled) {
        const daima_category_profile_t *profile = category_router_resolve(msg->intent);
        if (profile) {
            *model_override = profile->model;
            DAIMA_LOGI(TAG, "Category routing: intent=%s -> model=%s",
                       daima_intent_name(msg->intent), profile->model);
        }
    }
#endif
    return DAIMA_OK;
}

static agent_extension_hooks_t ext = {
    .name = "category_router",
    .before_run = before_run,
    .enabled = true,
};

__attribute__((constructor)) static void register_ext(void)
{
    agent_hooks_register(&ext);
}
