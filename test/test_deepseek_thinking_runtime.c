#include "paths.h"
#include "runtime.h"

#include <assert.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

int printk(const char *fmt, ...)
{
    (void)fmt;

    return 0;
}

static void mkdir_p(const char *path)
{
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0777);
            *p = '/';
        }
    }
    mkdir(tmp, 0777);
}

static void write_file_text(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    assert(f);
    fputs(text, f);
    fclose(f);
}

int main(void)
{
    char home[512];
    snprintf(home, sizeof(home), "/tmp/agent-deepseek-runtime-home-%ld", (long)getpid());
    setenv("AGENT_HOME", home, 1);
    paths_init();
    mkdir_p(path_config_dir());

    char cfg_path[512];
    snprintf(cfg_path, sizeof(cfg_path), "%s/config.json", path_config_dir());
    write_file_text(
        cfg_path,
        "{"
        "\"active_provider\":\"ingenic_local_deepseek\","
        "\"providers\":{"
        "\"ingenic_local_deepseek\":{"
        "\"model\":\"deepseek-v4-pro\","
        "\"api_mode\":\"anthropic_messages\","
        "\"thinking_mode\":\"high\","
        "\"reasoning_effort\":\"high\""
        "}"
        "}"
        "}\n");

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
