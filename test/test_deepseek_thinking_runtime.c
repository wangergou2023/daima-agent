#include "paths.h"
#include "runtime.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int printk(const char *fmt, ...)
{
    (void)fmt;

    return 0;
}

int main(void)
{
    setenv("AGENT_HOME", "/home/wangergou/code/github/agent", 1);
    paths_init();

    assert(runtime_config_init() == 0);
    assert(strcmp(runtime_config_get_active_provider(), "ingenic_local_deepseek") == 0);
    assert(strcmp(runtime_config_get_provider_thinking_mode(), "high") == 0);
    assert(strcmp(runtime_config_get_provider_reasoning_effort(), "high") == 0);

    const char *api_mode = runtime_config_get_provider_api_mode();
    printf("active_provider=%s\n", runtime_config_get_active_provider());
    printf("api_mode=%s\n", api_mode && api_mode[0] ? api_mode : "(default)");
    printf("thinking_mode=%s\n", runtime_config_get_provider_thinking_mode());
    printf("reasoning_effort=%s\n", runtime_config_get_provider_reasoning_effort());

    if (api_mode && strcmp(api_mode, "anthropic_messages") == 0) {
        printf("RESULT=ANTHROPIC_THINKING_PATH_CONFIGURED\n");
    } else {
        printf("RESULT=OPENAI_THINKING_PATH_AVAILABLE\n");
    }

    return 0;
}
