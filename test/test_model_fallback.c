#include "drivers/llm/model_fallback.h"
#include "drivers/llm/llm_proxy.h"
#include "paths.h"
#include "runtime.h"
#include "log.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <stdarg.h>

static char s_current_model[64] = "primary-model";
static int s_attempt_results[8];
static char s_attempt_models[8][64];
static int s_attempt_count;
static const char *s_expected_system_prompt;
static cJSON *s_expected_messages;
static const char *s_expected_tools_json;

const char *err_name(int err)
{
    switch (err) {
    case 0: return "0";
    case -EIO: return "-EIO";
    case -ETIMEDOUT: return "-ETIMEDOUT";
    default: return "ERR_UNKNOWN";
    }
}

int printk(const char *fmt, ...)
{
    (void)fmt;

    return 0;
}

void log_level_set(const char *tag, int level)
{
    (void)tag;
    (void)level;
}

void log_set_hook(log_hook_t hook)
{
    (void)hook;
}

int llm_set_model(const char *model)
{
    snprintf(s_current_model, sizeof(s_current_model), "%s", model ? model : "");
    return 0;
}

const char *llm_get_model_name(void)
{
    return s_current_model;
}

int llm_chat_tools(const char *system_prompt,
                           cJSON *messages,
                           const char *tools_json,
                           llm_response_t *resp)
{
    assert(system_prompt == s_expected_system_prompt);
    assert(messages == s_expected_messages);
    assert(tools_json == s_expected_tools_json);
    assert(resp != NULL);
    assert(s_attempt_count < 8);

    snprintf(s_attempt_models[s_attempt_count], sizeof(s_attempt_models[s_attempt_count]),
             "%s", s_current_model);
    int result = s_attempt_results[s_attempt_count];
    s_attempt_count++;
    memset(resp, 0, sizeof(*resp));
    if (!result) {
        resp->text = strdup("ok");
        resp->text_len = 2;
    }
    return result;
}

int llm_chat_tools_with_model(const char *system_prompt,
                                      cJSON *messages,
                                      const char *tools_json,
                                      const char *model_override,
                                      llm_response_t *resp)
{
    char previous[64];
    snprintf(previous, sizeof(previous), "%s", s_current_model);
    if (model_override && model_override[0]) {
        llm_set_model(model_override);
    }
    int err = llm_chat_tools(system_prompt, messages, tools_json, resp);
    llm_set_model(previous);
    return err;
}

void llm_response_free(llm_response_t *resp)
{
    if (!resp) return;
    free(resp->text);
    memset(resp, 0, sizeof(*resp));
}

static void write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    assert(f != NULL);
    fputs(text, f);
    fclose(f);
}

static void setup_home(void)
{
    setenv("AGENT_HOME", "/tmp/agent-model-fallback-test", 1);
    mkdir("/tmp/agent-model-fallback-test", 0755);
    mkdir("/tmp/agent-model-fallback-test/spiffs_data", 0755);
    mkdir("/tmp/agent-model-fallback-test/spiffs_data/config", 0755);
    remove("/tmp/agent-model-fallback-test/spiffs_data/config/config.json");
    remove("/tmp/agent-model-fallback-test/spiffs_data/config/category_routing.json");
    remove("/tmp/agent-model-fallback-test/spiffs_data/config/fallback_models.json");
    unsetenv("MODEL_FALLBACK_ENABLED");
}

static void reset_attempts(const char *primary_model)
{
    snprintf(s_current_model, sizeof(s_current_model), "%s", primary_model);
    memset(s_attempt_results, 0, sizeof(s_attempt_results));
    memset(s_attempt_models, 0, sizeof(s_attempt_models));
    s_attempt_count = 0;
}

static void test_defaults_used_when_config_missing(void)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/config.json", path_config_dir());
    write_file(path,
               "{\"active_provider\":\"ingenic_local_deepseek\","
               "\"providers\":{"
               "\"ingenic_local_deepseek\":{\"model\":\"deepseek-v4-pro\"},"
               "\"ingenic_local_kimi\":{\"model\":\"kimi-k2.6\"},"
               "\"bigmodel\":{\"model\":\"GLM-5.1\"}"
               "}}\n");
    assert(runtime_config_init() == 0);
    model_fallback_cfg_t cfg = model_fallback_load_cfg();
    assert(cfg.enabled);
    assert(cfg.model_count == 2);
    assert(strcmp(cfg.models[0], "kimi-k2.6") == 0);
    assert(strcmp(cfg.models[1], "GLM-5.1") == 0);
}

static void test_config_json_fallback_models_override_defaults(void)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/config.json", path_config_dir());
    write_file(path, "{\"fallback_models\":[\"backup-a\",\"backup-b\",\"backup-c\"]}");

    model_fallback_cfg_t cfg = model_fallback_load_cfg();
    assert(cfg.enabled);
    assert(cfg.model_count == 3);
    assert(strcmp(cfg.models[0], "backup-a") == 0);
    assert(strcmp(cfg.models[1], "backup-b") == 0);
    assert(strcmp(cfg.models[2], "backup-c") == 0);
}

static void test_env_zero_disables_fallback(void)
{
    setenv("MODEL_FALLBACK_ENABLED", "0", 1);
    model_fallback_cfg_t cfg = model_fallback_load_cfg();
    assert(!cfg.enabled);
}

static void test_retry_chain_stops_on_first_success_and_restores_primary(void)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/fallback_models.json", path_config_dir());
    write_file(path, "{\"fallback_models\":[\"backup-a\",\"backup-b\"]}");
    reset_attempts("primary-model");
    s_attempt_results[0] = -ETIMEDOUT;
    s_attempt_results[1] = -EIO;
    s_attempt_results[2] = 0;

    const char *system_prompt = "system";
    cJSON *messages = cJSON_CreateArray();
    const char *tools_json = "[]";
    s_expected_system_prompt = system_prompt;
    s_expected_messages = messages;
    s_expected_tools_json = tools_json;
    llm_response_t resp;

    int err = model_fallback_chat_with_fallback(system_prompt, messages, tools_json, &resp);
    assert(!err);
    assert(s_attempt_count == 3);
    assert(strcmp(s_attempt_models[0], "primary-model") == 0);
    assert(strcmp(s_attempt_models[1], "backup-a") == 0);
    assert(strcmp(s_attempt_models[2], "backup-b") == 0);
    assert(strcmp(llm_get_model_name(), "primary-model") == 0);
    llm_response_free(&resp);
    cJSON_Delete(messages);
}

static void test_all_failures_return_last_error_and_restore_primary(void)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/fallback_models.json", path_config_dir());
    write_file(path, "{\"fallback_models\":[\"backup-a\",\"backup-b\"]}");
    reset_attempts("primary-model");
    s_attempt_results[0] = -ETIMEDOUT;
    s_attempt_results[1] = -EIO;
    s_attempt_results[2] = -ETIMEDOUT;

    const char *system_prompt = "system";
    cJSON *messages = cJSON_CreateArray();
    const char *tools_json = "[]";
    s_expected_system_prompt = system_prompt;
    s_expected_messages = messages;
    s_expected_tools_json = tools_json;
    llm_response_t resp;

    int err = model_fallback_chat_with_fallback(system_prompt, messages, tools_json, &resp);
    assert(err == -ETIMEDOUT);
    assert(s_attempt_count == 3);
    assert(strcmp(llm_get_model_name(), "primary-model") == 0);
    cJSON_Delete(messages);
}

int main(void)
{
    setup_home();
    test_defaults_used_when_config_missing();
    remove("/tmp/agent-model-fallback-test/spiffs_data/config/config.json");
    test_config_json_fallback_models_override_defaults();
    remove("/tmp/agent-model-fallback-test/spiffs_data/config/config.json");
    test_env_zero_disables_fallback();
    unsetenv("MODEL_FALLBACK_ENABLED");
    test_retry_chain_stops_on_first_success_and_restores_primary();
    test_all_failures_return_last_error_and_restore_primary();
    return 0;
}
