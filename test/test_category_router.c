#include "router.h"
#include "paths.h"
#include "runtime.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void setup_home(void)
{
    setenv("AGENT_HOME", "/tmp/agent-category-router-test", 1);
    mkdir("/tmp/agent-category-router-test", 0755);
    mkdir("/tmp/agent-category-router-test/spiffs_data", 0755);
    mkdir("/tmp/agent-category-router-test/spiffs_data/config", 0755);
    remove("/tmp/agent-category-router-test/spiffs_data/config/config.json");
    remove("/tmp/agent-category-router-test/spiffs_data/config/category_routing.json");
}

static void write_config_json(void)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/config.json", path_config_dir());
    FILE *f = fopen(path, "w");
    assert(f != NULL);
    fputs("{\"category_routing\":{\"enabled\":true,\"profiles\":[{\"name\":\"cheap\",\"model\":\"cheap-model\",\"context_limit\":111,\"max_tokens\":222}],\"intent_map\":{\"qa\":\"cheap\"}}}", f);
    fclose(f);
}

static void write_provider_config_json(bool include_category_routing)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/config.json", path_config_dir());
    FILE *f = fopen(path, "w");
    assert(f != NULL);
    if (include_category_routing) {
        fputs("{"
              "\"active_provider\":\"ingenic_local_deepseek\","
              "\"providers\":{"
              "\"ingenic_local_deepseek\":{\"model\":\"deepseek-v4-pro\",\"context_limit_tokens\":128000,\"max_output_tokens\":8192},"
              "\"ingenic_local_kimi\":{\"model\":\"kimi-k2.6\",\"context_limit_tokens\":64000,\"max_output_tokens\":4096}"
              "},"
              "\"category_routing\":{"
              "\"enabled\":true,"
              "\"profiles\":{\"quick\":\"ingenic_local_kimi\",\"deep\":\"ingenic_local_deepseek\"},"
              "\"intent_map\":{\"qa\":\"quick\",\"implement\":\"deep\",\"investigate\":\"deep\",\"fix\":\"deep\",\"open\":\"quick\"}"
              "}"
              "}", f);
    } else {
        fputs("{"
              "\"active_provider\":\"ingenic_local_deepseek\","
              "\"providers\":{"
              "\"ingenic_local_deepseek\":{\"model\":\"deepseek-v4-pro\",\"context_limit_tokens\":128000,\"max_output_tokens\":8192},"
              "\"ingenic_local_kimi\":{\"model\":\"kimi-k2.6\",\"context_limit_tokens\":64000,\"max_output_tokens\":4096}"
              "}"
              "}", f);
    }
    fclose(f);
}

static void write_category_config_json(void)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/config.json", path_config_dir());
    FILE *f = fopen(path, "w");
    assert(f != NULL);
    fputs("{"
          "\"active_provider\":\"ingenic_local_deepseek\","
          "\"providers\":{"
          "\"ingenic_local_deepseek\":{\"model\":\"deepseek-v4-pro\",\"context_limit_tokens\":128000,\"max_output_tokens\":8192},"
          "\"ingenic_local_kimi\":{\"model\":\"kimi-k2.6\",\"context_limit_tokens\":64000,\"max_output_tokens\":4096},"
          "\"cloud_glm\":{\"model\":\"GLM-5.1\",\"context_limit_tokens\":96000,\"max_output_tokens\":6144}"
          "},"
          "\"category_routing\":{"
          "\"enabled\":true,"
          "\"categories\":{"
          "\"deep\":{\"model_preference\":[\"missing-model\",\"GLM-5.1\",\"deepseek-v4-pro\"]},"
          "\"quick\":{\"model_preference\":[\"kimi-k2.6\",\"deepseek-v4-pro\"]}"
          "},"
          "\"intent_map\":{\"qa\":\"quick\",\"implement\":\"deep\",\"investigate\":\"deep\",\"fix\":\"deep\",\"open\":\"quick\"},"
          "\"role_model_map\":{\"FAST\":\"quick\",\"PLANNER\":\"deep\",\"EXECUTOR\":\"deep\",\"REVIEWER\":\"deep\"}"
          "}"
          "}", f);
    fclose(f);
}

static void write_unavailable_category_config_json(void)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/config.json", path_config_dir());
    FILE *f = fopen(path, "w");
    assert(f != NULL);
    fputs("{"
          "\"active_provider\":\"ingenic_local_deepseek\","
          "\"providers\":{"
          "\"ingenic_local_deepseek\":{\"model\":\"deepseek-v4-pro\",\"context_limit_tokens\":128000,\"max_output_tokens\":8192},"
          "\"ingenic_local_kimi\":{\"model\":\"kimi-k2.6\",\"context_limit_tokens\":64000,\"max_output_tokens\":4096}"
          "},"
          "\"category_routing\":{"
          "\"enabled\":true,"
          "\"categories\":{\"quick\":{\"model_preference\":[\"missing-a\",\"missing-b\"]}},"
          "\"intent_map\":{\"qa\":\"quick\"}"
          "}"
          "}", f);
    fclose(f);
}

static void test_default_profiles_route_expected_intents(void)
{
    write_provider_config_json(false);
    assert(runtime_config_init() == 0);
    category_router_cfg_t cfg = category_router_load_and_get_cfg();
    assert(cfg.enabled);
    assert(cfg.profile_count == 2);

    const category_profile_t *qa = category_router_resolve(INTENT_QA);
    assert(qa != NULL);
    assert(strcmp(qa->name, "quick") == 0);
    assert(strcmp(qa->model, "kimi-k2.6") == 0);

    const category_profile_t *impl = category_router_resolve(INTENT_IMPLEMENT);
    assert(impl != NULL);
    assert(strcmp(impl->name, "deep") == 0);
    assert(strcmp(impl->model, "deepseek-v4-pro") == 0);
    assert(impl->context_limit == 128000);
    assert(impl->max_tokens == 8192);
}

static void test_config_json_category_routing_provider_names_resolve_to_models(void)
{
    write_provider_config_json(true);
    assert(runtime_config_init() == 0);
    category_router_cfg_t cfg = category_router_load_and_get_cfg();
    assert(cfg.enabled);
    assert(cfg.profile_count == 2);

    const category_profile_t *qa = category_router_resolve(INTENT_QA);
    assert(qa != NULL);
    assert(strcmp(qa->name, "quick") == 0);
    assert(strcmp(qa->model, "kimi-k2.6") == 0);

    const category_profile_t *impl = category_router_resolve(INTENT_IMPLEMENT);
    assert(impl != NULL);
    assert(strcmp(impl->name, "deep") == 0);
    assert(strcmp(impl->model, "deepseek-v4-pro") == 0);
}

static void test_config_json_categories_resolve_first_available_preferred_model(void)
{
    write_category_config_json();
    assert(runtime_config_init() == 0);
    category_router_cfg_t cfg = category_router_load_and_get_cfg();
    assert(cfg.enabled);
    assert(cfg.profile_count == 2);

    const category_profile_t *qa = category_router_resolve(INTENT_QA);
    assert(qa != NULL);
    assert(strcmp(qa->name, "quick") == 0);
    assert(strcmp(qa->model, "kimi-k2.6") == 0);
    assert(qa->context_limit == 64000);
    assert(qa->max_tokens == 4096);

    const category_profile_t *impl = category_router_resolve(INTENT_IMPLEMENT);
    assert(impl != NULL);
    assert(strcmp(impl->name, "deep") == 0);
    assert(strcmp(impl->model, "GLM-5.1") == 0);
    assert(impl->context_limit == 96000);
    assert(impl->max_tokens == 6144);

    const category_profile_t *fast = category_router_resolve_for_role(AGENT_ROLE_FAST);
    assert(fast != NULL);
    assert(strcmp(fast->name, "quick") == 0);
}

static void test_unavailable_category_model_preferences_fall_back_to_active_provider(void)
{
    write_unavailable_category_config_json();
    assert(runtime_config_init() == 0);
    category_router_cfg_t cfg = category_router_load_and_get_cfg();
    assert(cfg.enabled);
    assert(cfg.profile_count == 1);

    const category_profile_t *qa = category_router_resolve(INTENT_QA);
    assert(qa != NULL);
    assert(strcmp(qa->name, "quick") == 0);
    assert(strcmp(qa->model, "deepseek-v4-pro") == 0);
    assert(qa->context_limit == 128000);
    assert(qa->max_tokens == 8192);
}

static void test_config_json_category_routing_overrides_defaults(void)
{
    write_config_json();
    category_router_cfg_t cfg = category_router_load_and_get_cfg();
    assert(cfg.enabled);
    assert(cfg.profile_count == 1);

    const category_profile_t *qa = category_router_resolve(INTENT_QA);
    assert(qa != NULL);
    assert(strcmp(qa->name, "cheap") == 0);
    assert(strcmp(qa->model, "cheap-model") == 0);
    assert(qa->context_limit == 111);
    assert(qa->max_tokens == 222);

    assert(category_router_resolve(INTENT_IMPLEMENT) == NULL);
}

static void test_invalid_intent_returns_null(void)
{
    assert(category_router_resolve(INTENT_COUNT) == NULL);
    assert(category_router_resolve((enum intent)-1) == NULL);
}

int main(void)
{
    setup_home();
    test_default_profiles_route_expected_intents();
    test_invalid_intent_returns_null();
    category_router_reset_for_test();
    test_config_json_category_routing_provider_names_resolve_to_models();
    category_router_reset_for_test();
    test_config_json_categories_resolve_first_available_preferred_model();
    category_router_reset_for_test();
    test_unavailable_category_model_preferences_fall_back_to_active_provider();
    category_router_reset_for_test();
    test_config_json_category_routing_overrides_defaults();
    return 0;
}
