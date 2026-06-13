#include "hooks.h"
#include "roles.h"
#include "state.h"
#include "autoconf.h"
#include "linux/module.h"
#include "log.h"

#include <stdio.h>
#include <string.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("daima");
MODULE_DESCRIPTION("Agent Extension: agent_roles");

static const char *TAG = "ext_agent_roles";

static agent_role_t active_role_for_plan(const agent_role_t roles[3], int role_count)
{
    if (role_count <= 0) return AGENT_ROLE_FAST;
#if AGENT_EXTENSIONS_ENABLED
    if (agent_extension_state_has_reviewed_plan() && role_count > 1) return roles[1];
#endif
    return roles[0];
}

static daima_err_t on_intent(daima_msg_t *msg)
{
#if AGENT_EXTENSIONS_ENABLED
    agent_role_t roles[3] = {0};
    int role_count = agent_roles_for_intent(msg->intent, roles);
    agent_role_t active_role = active_role_for_plan(roles, role_count);
    agent_extension_state_set_roles(roles, role_count, active_role);
    if (role_count > 0) {
        DAIMA_LOGI(TAG, "Agent roles for intent=%s: %s (chain of %d)",
                   daima_intent_name(msg->intent), agent_role_name(roles[0]), role_count);
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
    if (!system_prompt || system_prompt_size == 0 || agent_extension_state_role_count() <= 0) return DAIMA_OK;
    agent_role_t role = agent_extension_state_active_role();
    size_t off = strnlen(system_prompt, system_prompt_size - 1);
    if (off >= system_prompt_size - 1) return DAIMA_OK;
    int written = snprintf(system_prompt + off, system_prompt_size - off,
                           "\n\n## 当前角色: %s\n%s\n",
                           agent_role_name(role), agent_role_prompt_suffix(role));
    if (written < 0 || (size_t)written >= system_prompt_size - off) {
        system_prompt[system_prompt_size - 1] = '\0';
    }
#endif
    return DAIMA_OK;
}

static agent_extension_hooks_t ext = {
    .name = "agent_roles",
    .on_intent = on_intent,
    .on_prepare = on_prepare,
    .enabled = true,
};

__attribute__((constructor)) static void register_ext(void)
{
    agent_hooks_register(&ext);
}
