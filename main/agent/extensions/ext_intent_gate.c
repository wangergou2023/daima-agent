#include "agent/agent_hooks.h"
#include "agent/intent_gate.h"
#include "daima_config.h"
#include "daima_log.h"

static const char *TAG = "ext_intent_gate";

static daima_err_t on_intent(daima_msg_t *msg)
{
#if AGENT_EXTENSIONS_ENABLED
    intent_gate_classify(msg->content, &msg->intent);
    DAIMA_LOGI(TAG, "Intent classified: %s -> %s", msg->content, daima_intent_name(msg->intent));
#endif
    return DAIMA_OK;
}

static agent_extension_hooks_t ext = {
    .name = "intent_gate",
    .on_intent = on_intent,
    .enabled = true,
};

__attribute__((constructor)) static void register_ext(void)
{
    agent_hooks_register(&ext);
}
