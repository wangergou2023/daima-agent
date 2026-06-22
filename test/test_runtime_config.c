#include "paths.h"
#include "runtime.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cjson.h"

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

    char cfg_path[512];
    snprintf(cfg_path, sizeof(cfg_path), "%s/config.json", path_config_dir());

    write_file_text(
        cfg_path,
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
        cfg_path,
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
        cfg_path,
        "{"
        "\"common\":{\"max_output_tokens\":8192},"
        "\"active_provider\":\"p1\","
        "\"providers\":{\"p1\":{\"model\":\"m\",\"api_key\":\"k\",\"openai_base_url\":\"http://example.test\",\"max_output_tokens\":128000}}"
        "}\n");
    assert(runtime_config_init() == 0);
    assert(runtime_config_get_max_output_tokens() == 128000);

    write_file_text(
        cfg_path,
        "{"
        "\"active_provider\":\"p1\","
        "\"providers\":{\"p1\":{\"model\":\"m\",\"api_key\":\"k\",\"openai_base_url\":\"http://example.test\"}}"
        "}\n");
    assert(runtime_config_init() == 0);
    assert(runtime_config_get_max_output_tokens() == 4096);
    assert(runtime_config_set_terminal_security_level("plan") == 0);
    assert(strcmp(runtime_config_get_terminal_security_level(), "plan") == 0);

    FILE *updated = fopen(cfg_path, "r");
    assert(updated);
    char updated_text[1024];
    size_t updated_len = fread(updated_text, 1, sizeof(updated_text) - 1, updated);
    fclose(updated);
    updated_text[updated_len] = '\0';
    cJSON *updated_root = cJSON_Parse(updated_text);
    assert(updated_root);
    cJSON *common = cJSON_GetObjectItemCaseSensitive(updated_root, "common");
    assert(common && cJSON_IsObject(common));
    const cJSON *level = cJSON_GetObjectItemCaseSensitive(common, "terminal_security_level");
    assert(cJSON_IsString(level));
    assert(strcmp(level->valuestring, "plan") == 0);
    cJSON_Delete(updated_root);

    printf("runtime_config tests passed\n");
    return 0;
}
