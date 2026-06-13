#include "runtime_internal.h"

#include <stdio.h>
#include <string.h>

#include "autoconf.h"
#include "log.h"

static const char *TAG = "runtime_config_sections";

static void apply_common_values(runtime_config_state_t *cfg, const cJSON *common)
{
    int value = 0;
    bool bool_value = false;

    if (!cfg || !common || !cJSON_IsObject(common)) {
        return;
    }

    if (runtime_config_json_read_int(common, "web_port", &value)) {
        cfg->web_port = runtime_config_clamp_int(value, 1, 65535, cfg->web_port);
    }
    if (runtime_config_json_read_int(common, "session_max_msgs", &value)) {
        cfg->session_max_msgs = runtime_config_clamp_int(value, 8, DAIMA_SESSION_MAX_MSGS, cfg->session_max_msgs);
    }
    if (runtime_config_json_read_int(common, "context_limit_tokens", &value) && value >= 1 && value <= 2000000) {
        cfg->common_context_limit_tokens = value;
    }
    if (runtime_config_json_read_int(common, "max_output_tokens", &value)) {
        cfg->common_max_output_tokens = runtime_config_clamp_int(value, 256, 131072, cfg->common_max_output_tokens);
    }
    if (runtime_config_json_read_int(common, "compress_trigger_msgs", &value)) {
        cfg->compress_trigger_msgs = runtime_config_clamp_int(value, 4, 10000, cfg->compress_trigger_msgs);
    }
    if (runtime_config_json_read_int(common, "compress_keep_msgs", &value)) {
        cfg->compress_keep_msgs = runtime_config_clamp_int(value, 2, 1000, cfg->compress_keep_msgs);
    }
    if (runtime_config_json_read_bool(common, "learning_review_enabled", &bool_value)) {
        cfg->learning_review_enabled = bool_value;
    }
    if (runtime_config_json_read_int(common, "cron_check_interval_ms", &value)) {
        cfg->cron_check_interval_ms = runtime_config_clamp_int(value, 1000, 86400000, cfg->cron_check_interval_ms);
    }
    if (runtime_config_json_read_int(common, "heartbeat_interval_ms", &value)) {
        cfg->heartbeat_interval_ms = runtime_config_clamp_int(value, 1000, 86400000, cfg->heartbeat_interval_ms);
    }
    runtime_config_json_copy_string(common, "timezone", cfg->timezone, sizeof(cfg->timezone));
    runtime_config_json_copy_string(common,
                                    "terminal_security_level",
                                    cfg->terminal_security_level,
                                    sizeof(cfg->terminal_security_level));
}

static void apply_web_values(runtime_config_state_t *cfg, const cJSON *root)
{
    if (!cfg || !root || !cJSON_IsObject(root)) {
        return;
    }

    runtime_config_json_copy_string(root,
                                    "default_pet_package_id",
                                    cfg->web_default_pet_package_id,
                                    sizeof(cfg->web_default_pet_package_id));
}

static void apply_provider_values(runtime_config_state_t *cfg,
                                  const char *provider_name,
                                  const cJSON *provider)
{
    bool bool_value = false;
    int int_value = 0;

    if (!cfg || !provider_name || !provider_name[0] || !provider || !cJSON_IsObject(provider)) {
        return;
    }

    snprintf(cfg->active_provider, sizeof(cfg->active_provider), "%s", provider_name);
    runtime_config_json_copy_string(provider, "api_key", cfg->provider_api_key, sizeof(cfg->provider_api_key));
    runtime_config_json_copy_string(provider, "model", cfg->provider_model, sizeof(cfg->provider_model));
    runtime_config_json_copy_string(provider, "openai_base_url", cfg->provider_openai_base_url, sizeof(cfg->provider_openai_base_url));
    runtime_config_json_copy_string(provider, "api_mode", cfg->provider_api_mode, sizeof(cfg->provider_api_mode));
    runtime_config_json_copy_string(provider, "thinking_mode", cfg->provider_thinking_mode, sizeof(cfg->provider_thinking_mode));
    runtime_config_json_copy_string(provider, "reasoning_effort", cfg->provider_reasoning_effort, sizeof(cfg->provider_reasoning_effort));
    if (runtime_config_json_read_int(provider, "context_limit_tokens", &int_value) && int_value >= 1 && int_value <= 2000000) {
        cfg->provider_context_limit_tokens = int_value;
    }
    if (runtime_config_json_read_int(provider, "max_output_tokens", &int_value)) {
        cfg->provider_max_output_tokens = runtime_config_clamp_int(int_value, 256, 131072, cfg->provider_max_output_tokens);
    }
    if (runtime_config_json_read_int(provider, "request_timeout_ms", &int_value)) {
        cfg->provider_request_timeout_ms = runtime_config_clamp_int(int_value, 1000, 900000, cfg->provider_request_timeout_ms);
    }
    if (runtime_config_json_read_bool(provider, "needs_reasoning_content", &bool_value)) {
        cfg->provider_needs_reasoning_content = bool_value;
    }

    DAIMA_LOGI(TAG, "Runtime config applied provider: %s", provider_name);
}

static void collect_provider_entries(runtime_config_state_t *cfg, const cJSON *providers)
{
    const cJSON *entry = NULL;

    if (!cfg || !providers || !cJSON_IsObject(providers)) {
        return;
    }

    cfg->provider_count = 0;
    cJSON_ArrayForEach(entry, (cJSON *)providers) {
        if (!entry->string || !entry->string[0] || !cJSON_IsObject(entry)) {
            continue;
        }
        if (cfg->provider_count >= RUNTIME_PROVIDER_MAX) {
            DAIMA_LOGW(TAG, "Runtime config provider list truncated at %d", RUNTIME_PROVIDER_MAX);
            break;
        }
        runtime_provider_entry_t *out = &cfg->providers[cfg->provider_count];
        snprintf(out->name, sizeof(out->name), "%s", entry->string);
        runtime_config_json_copy_string(entry, "model", out->model, sizeof(out->model));
        cfg->provider_count++;
    }
}

static void apply_feishu_values(runtime_config_state_t *cfg, const cJSON *root)
{
    if (!cfg || !root || !cJSON_IsObject(root)) {
        return;
    }
    runtime_config_json_copy_string(root, "app_id", cfg->feishu_app_id, sizeof(cfg->feishu_app_id));
    runtime_config_json_copy_string(root, "app_secret", cfg->feishu_app_secret, sizeof(cfg->feishu_app_secret));
    runtime_config_json_copy_string(root, "default_chat_id", cfg->feishu_default_chat_id, sizeof(cfg->feishu_default_chat_id));
}

static void apply_audio_values(runtime_config_state_t *cfg, const cJSON *root)
{
    int value = 0;

    if (!cfg || !root || !cJSON_IsObject(root)) {
        return;
    }

    runtime_config_json_copy_string(root, "bigmodel_api_key", cfg->bigmodel_api_key, sizeof(cfg->bigmodel_api_key));
    if (runtime_config_json_read_int(root, "ai_vol", &value)) {
        cfg->audio_ai_vol = runtime_config_clamp_int(value, 0, 120, cfg->audio_ai_vol);
    }
    if (runtime_config_json_read_int(root, "ai_gain", &value)) {
        cfg->audio_ai_gain = runtime_config_clamp_int(value, 0, 120, cfg->audio_ai_gain);
    }
    if (runtime_config_json_read_int(root, "ao_vol", &value)) {
        cfg->audio_ao_vol = runtime_config_clamp_int(value, 0, 120, cfg->audio_ao_vol);
    }
    if (runtime_config_json_read_int(root, "ao_gain", &value)) {
        cfg->audio_ao_gain = runtime_config_clamp_int(value, 0, 120, cfg->audio_ao_gain);
    }
    if (runtime_config_json_read_int(root, "voice_record_ms", &value)) {
        cfg->voice_record_ms = runtime_config_clamp_int(value, 500, 600000, cfg->voice_record_ms);
    }
}

static void apply_mips_values(runtime_config_state_t *cfg, const cJSON *root)
{
    int value = 0;

    if (!cfg || !root || !cJSON_IsObject(root)) {
        return;
    }

    if (runtime_config_json_read_int(root, "wake_gpio_num", &value)) {
        cfg->wake_gpio_num = runtime_config_clamp_int(value, 0, 1024, cfg->wake_gpio_num);
    }
    if (runtime_config_json_read_int(root, "wake_gpio_active_low", &value)) {
        cfg->wake_gpio_active_low = runtime_config_clamp_int(value, 0, 1, cfg->wake_gpio_active_low);
    }
    if (runtime_config_json_read_int(root, "wake_gpio_poll_ms", &value)) {
        cfg->wake_gpio_poll_ms = runtime_config_clamp_int(value, 1, 60000, cfg->wake_gpio_poll_ms);
    }
    if (runtime_config_json_read_int(root, "wake_gpio_debounce_ms", &value)) {
        cfg->wake_gpio_debounce_ms = runtime_config_clamp_int(value, 0, 60000, cfg->wake_gpio_debounce_ms);
    }
}

static void apply_active_provider(runtime_config_state_t *cfg,
                                  const cJSON *providers,
                                  const char *active_provider)
{
    const cJSON *selected = NULL;

    if (!cfg || !providers || !cJSON_IsObject(providers) || !active_provider || !active_provider[0]) {
        DAIMA_LOGW(TAG, "Runtime config missing active_provider");
        return;
    }

    selected = runtime_config_get_object_item(providers, active_provider);
    if (!selected || !cJSON_IsObject(selected)) {
        DAIMA_LOGW(TAG, "Runtime config provider not found: %s", active_provider);
        return;
    }

    apply_provider_values(cfg, active_provider, selected);
}

void runtime_config_apply_values(runtime_config_state_t *cfg, const cJSON *root)
{
    const cJSON *common = NULL;
    const cJSON *providers = NULL;
    const cJSON *feishu = NULL;
    const cJSON *audio = NULL;
    const cJSON *mips = NULL;
    const cJSON *web = NULL;
    const char *active_provider = NULL;

    if (!cfg || !root || !cJSON_IsObject(root)) {
        return;
    }

    common = runtime_config_get_object_item(root, "common");
    providers = runtime_config_get_object_item(root, "providers");
    feishu = runtime_config_get_object_item(root, "feishu");
    audio = runtime_config_get_object_item(root, "audio");
    mips = runtime_config_get_object_item(root, "mips");
    web = runtime_config_get_object_item(root, "web");
    active_provider = cJSON_GetStringValue((cJSON *)runtime_config_get_object_item(root, "active_provider"));

    apply_feishu_values(cfg, feishu);
    apply_audio_values(cfg, audio);
    apply_mips_values(cfg, mips);
    apply_web_values(cfg, web);
    apply_common_values(cfg, common);

    if (providers && cJSON_IsObject(providers)) {
        collect_provider_entries(cfg, providers);
        apply_active_provider(cfg, providers, active_provider);
    }
}
