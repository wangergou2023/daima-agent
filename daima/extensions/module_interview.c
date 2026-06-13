#include "hooks.h"
#include "turn_finish.h"
#include "state.h"
#include "interview.h"
#include "autoconf.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>

static const char *TAG = "ext_prometheus";

static daima_err_t replace_run(daima_msg_t *msg, char *system_prompt,
                               cJSON *messages, const char *tools_json,
                               char **out_final_text)
{
    (void)system_prompt;
    (void)messages;
    (void)tools_json;
#if AGENT_EXTENSIONS_ENABLED
    if (agent_extension_state_role_count() <= 1 && msg->intent == DAIMA_INTENT_IMPLEMENT) {
        prometheus_state_t p_state;
        if (prometheus_check_needs_interview(msg->content, &p_state) == DAIMA_OK &&
            p_state.needs_interview) {
            DAIMA_LOGI(TAG, "Prometheus: interview mode, asking questions");
            *out_final_text = strdup(p_state.questions);
            return DAIMA_OK;
        }
    }
#endif
    return DAIMA_FAIL;
}

static agent_extension_hooks_t ext = {
    .name = "prometheus",
    .replace_run = replace_run,
    .enabled = true,
};

__attribute__((constructor)) static void register_ext(void)
{
    agent_hooks_register(&ext);
}
