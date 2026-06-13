#include "core/hooks.h"

static agent_extension_hooks_t *s_extensions[AGENT_MAX_EXTENSIONS];
static size_t s_extension_count;
static bool s_initialized;

void agent_hooks_init(void)
{
#ifdef DAIMA_AGENT_HOOKS_TEST_RESET
    s_extension_count = 0;
    for (size_t i = 0; i < AGENT_MAX_EXTENSIONS; i++) {
        s_extensions[i] = NULL;
    }
#endif
    s_initialized = true;
}

void agent_hooks_register(agent_extension_hooks_t *hooks)
{
    if (!hooks) {
        return;
    }
    if (s_extension_count >= AGENT_MAX_EXTENSIONS) {
        return;
    }
    s_extensions[s_extension_count++] = hooks;
    if (!s_initialized) {
        s_initialized = true;
    }
}

static bool extension_enabled(const agent_extension_hooks_t *hooks)
{
    return hooks && hooks->enabled;
}

daima_err_t agent_hooks_trigger_intent(daima_msg_t *msg)
{
    for (size_t i = 0; i < s_extension_count; i++) {
        agent_extension_hooks_t *hooks = s_extensions[i];
        if (!extension_enabled(hooks) || !hooks->on_intent) continue;
        daima_err_t err = hooks->on_intent(msg);
        if (err != DAIMA_OK) return err;
    }
    return DAIMA_OK;
}

daima_err_t agent_hooks_trigger_prepare(daima_msg_t *msg,
    char *system_prompt, size_t system_prompt_size, cJSON *messages)
{
    for (size_t i = 0; i < s_extension_count; i++) {
        agent_extension_hooks_t *hooks = s_extensions[i];
        if (!extension_enabled(hooks) || !hooks->on_prepare) continue;
        daima_err_t err = hooks->on_prepare(msg, system_prompt, system_prompt_size, messages);
        if (err != DAIMA_OK) return err;
    }
    return DAIMA_OK;
}

daima_err_t agent_hooks_trigger_before_run(daima_msg_t *msg,
    const char **model_override, const char *tools_json)
{
    for (size_t i = 0; i < s_extension_count; i++) {
        agent_extension_hooks_t *hooks = s_extensions[i];
        if (!extension_enabled(hooks) || !hooks->before_run) continue;
        daima_err_t err = hooks->before_run(msg, model_override, tools_json);
        if (err != DAIMA_OK) return err;
    }
    return DAIMA_OK;
}

daima_err_t agent_hooks_trigger_replace_run(daima_msg_t *msg,
    char *system_prompt, cJSON *messages, const char *tools_json,
    char **out_final_text)
{
    for (size_t i = 0; i < s_extension_count; i++) {
        agent_extension_hooks_t *hooks = s_extensions[i];
        if (!extension_enabled(hooks) || !hooks->replace_run) continue;
        daima_err_t err = hooks->replace_run(msg, system_prompt, messages, tools_json, out_final_text);
        if (err == DAIMA_OK) return DAIMA_OK;
    }
    return DAIMA_FAIL;
}

void agent_hooks_trigger_finish(daima_msg_t *msg, const char *response)
{
    for (size_t i = 0; i < s_extension_count; i++) {
        agent_extension_hooks_t *hooks = s_extensions[i];
        if (!extension_enabled(hooks) || !hooks->on_finish) continue;
        hooks->on_finish(msg, response);
    }
}
