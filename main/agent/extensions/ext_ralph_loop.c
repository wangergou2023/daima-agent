#include "agent/agent_hooks.h"
#include "agent/ralph_loop.h"
#include "daima_config.h"

#include <stdlib.h>
#include <string.h>

static daima_err_t on_prepare(daima_msg_t *msg, char *system_prompt,
                              size_t system_prompt_size, cJSON *messages)
{
    (void)system_prompt;
    (void)system_prompt_size;
    (void)messages;
    (void)msg;
    return DAIMA_OK;
}

static void on_finish(daima_msg_t *msg, const char *response)
{
    (void)msg;
    (void)response;
}

bool agent_extension_ralph_should_append_warning(daima_msg_t *msg, int iteration, char **io_final_text)
{
#if AGENT_EXTENSIONS_ENABLED
    static const char warning[] = "\n\n⚠️ 还有未完成的任务，请继续。";
    if (!msg || !io_final_text) return false;
    const char *final_text = *io_final_text ? *io_final_text : "";
    if (!ralph_loop_should_continue(msg->chat_id, iteration, final_text)) return false;
    size_t final_len = strlen(final_text);
    size_t warning_len = sizeof(warning) - 1;
    char *with_warning = malloc(final_len + warning_len + 1);
    if (!with_warning) return false;
    memcpy(with_warning, final_text, final_len);
    memcpy(with_warning + final_len, warning, warning_len + 1);
    free(*io_final_text);
    *io_final_text = with_warning;
    return true;
#else
    (void)msg;
    (void)iteration;
    (void)io_final_text;
    return false;
#endif
}

static agent_extension_hooks_t ext = {
    .name = "ralph_loop",
    .on_prepare = on_prepare,
    .on_finish = on_finish,
    .enabled = true,
};

__attribute__((constructor)) static void register_ext(void)
{
    agent_hooks_register(&ext);
}
