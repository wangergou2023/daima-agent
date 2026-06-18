#include "paths.h"
#include "runtime.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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
    snprintf(home, sizeof(home), "/tmp/agent-runtime-config-home-%ld", (long)getpid());
    setenv("AGENT_HOME", home, 1);
    paths_init();
    mkdir_p(path_config_dir());

    write_file_text(
        path_runtime_config_file(),
        "{"
        "\"common\":{\"max_output_tokens\":8192},"
        "\"active_provider\":\"p1\","
        "\"providers\":{\"p1\":{\"model\":\"m\",\"api_key\":\"k\",\"openai_base_url\":\"http://example.test\"}}"
        "}\n");
    assert(runtime_config_init() == 0);
    assert(runtime_config_get_max_output_tokens() == 8192);
    assert(strcmp(runtime_config_get_terminal_security_level(), "build") == 0);
    assert(!runtime_config_get_learning_review_enabled());
    assert(strcmp(runtime_config_get_provider_reasoning_effort(), "") == 0);

    write_file_text(
        path_runtime_config_file(),
        "{"
        "\"common\":{\"max_output_tokens\":8192,\"terminal_security_level\":\"plan\",\"learning_review_enabled\":true},"
        "\"active_provider\":\"p1\","
        "\"providers\":{\"p1\":{\"model\":\"m\",\"api_key\":\"k\",\"openai_base_url\":\"http://example.test\",\"max_output_tokens\":16384,\"request_timeout_ms\":300000,\"thinking_mode\":\"high\",\"reasoning_effort\":\"high\"}}"
        "}\n");
    assert(runtime_config_init() == 0);
    assert(runtime_config_get_max_output_tokens() == 16384);
    assert(runtime_config_get_request_timeout_ms() == 300000);
    assert(strcmp(runtime_config_get_terminal_security_level(), "plan") == 0);
    assert(runtime_config_get_learning_review_enabled());
    assert(strcmp(runtime_config_get_provider_thinking_mode(), "high") == 0);
    assert(strcmp(runtime_config_get_provider_reasoning_effort(), "high") == 0);

    write_file_text(
        path_runtime_config_file(),
        "{"
        "\"common\":{\"max_output_tokens\":8192},"
        "\"active_provider\":\"p1\","
        "\"providers\":{\"p1\":{\"model\":\"m\",\"api_key\":\"k\",\"openai_base_url\":\"http://example.test\",\"max_output_tokens\":128000}}"
        "}\n");
    assert(runtime_config_init() == 0);
    assert(runtime_config_get_max_output_tokens() == 128000);

    write_file_text(
        path_runtime_config_file(),
        "{"
        "\"active_provider\":\"p1\","
        "\"providers\":{\"p1\":{\"model\":\"m\",\"api_key\":\"k\",\"openai_base_url\":\"http://example.test\"}}"
        "}\n");
    assert(runtime_config_init() == 0);
    assert(runtime_config_get_max_output_tokens() == 4096);

    printf("runtime_config tests passed\n");
    return 0;
}
