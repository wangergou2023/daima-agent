/* 运行时配置管理：从 spiffs_data/config/config.json 加载全局配置，提供 getter 函数。
 * 配置段包括 common(通用)、providers(LLM提供者)、feishu、audio、mips、web 等。
 * 所有整型值均经过 clamp 钳制，字符串有默认 fallback。 */

#include "runtime.h"
#include "runtime_defaults.h"
#include "runtime_internal.h"

#include <string.h>

#include "autoconf.h"
#include "linux/kernel.h"

runtime_config_state_t s_cfg;

static const char *DEFAULT_TIMEZONE = "CST-8";
static const char *DEFAULT_LLM_MODEL = "kimi-k2.5";
static const char *DEFAULT_WEB_PET_PACKAGE_ID = "guga.codex-pet";
static const char *DEFAULT_TERMINAL_SECURITY_LEVEL = "build";

enum {
    DEFAULT_WEB_PORT = 1234,
    DEFAULT_COMPRESS_TRIGGER_MSGS = 12,
    DEFAULT_COMPRESS_KEEP_MSGS = 6,
    DEFAULT_CRON_CHECK_INTERVAL_MS = 60 * 1000,
    DEFAULT_HEARTBEAT_INTERVAL_MS = 30 * 60 * 1000,
    DEFAULT_AUDIO_AI_VOL = 60,
    DEFAULT_AUDIO_AI_GAIN = 23,
    DEFAULT_AUDIO_AO_VOL = 80,
    DEFAULT_AUDIO_AO_GAIN = 20,
    DEFAULT_VOICE_RECORD_MS = 3000,
    DEFAULT_WAKE_GPIO_NUM = 64,
    DEFAULT_WAKE_GPIO_ACTIVE_LOW = 1,
    DEFAULT_WAKE_GPIO_POLL_MS = 20,
    DEFAULT_WAKE_GPIO_DEBOUNCE_MS = 200,
    DEFAULT_LLM_REQUEST_TIMEOUT_MS = 300 * 1000,
};

const char *runtime_config_default_timezone(void)
{
    return DEFAULT_TIMEZONE;
}

const char *runtime_config_default_llm_model(void)
{
    return DEFAULT_LLM_MODEL;
}

const char *runtime_config_default_web_pet_package_id(void)
{
    return DEFAULT_WEB_PET_PACKAGE_ID;
}

const char *runtime_config_default_terminal_security_level(void)
{
    return DEFAULT_TERMINAL_SECURITY_LEVEL;
}

int runtime_config_default_request_timeout_ms(void)
{
    return DEFAULT_LLM_REQUEST_TIMEOUT_MS;
}

void runtime_config_reset_defaults(runtime_config_state_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->web_port = DEFAULT_WEB_PORT;
    cfg->session_max_msgs = SESSION_MAX_MSGS;
    cfg->compress_trigger_msgs = DEFAULT_COMPRESS_TRIGGER_MSGS;
    cfg->compress_keep_msgs = DEFAULT_COMPRESS_KEEP_MSGS;
    cfg->learning_review_enabled = false;
    cfg->cron_check_interval_ms = DEFAULT_CRON_CHECK_INTERVAL_MS;
    cfg->heartbeat_interval_ms = DEFAULT_HEARTBEAT_INTERVAL_MS;
    cfg->audio_ai_vol = DEFAULT_AUDIO_AI_VOL;
    cfg->audio_ai_gain = DEFAULT_AUDIO_AI_GAIN;
    cfg->audio_ao_vol = DEFAULT_AUDIO_AO_VOL;
    cfg->audio_ao_gain = DEFAULT_AUDIO_AO_GAIN;
    cfg->voice_record_ms = DEFAULT_VOICE_RECORD_MS;
    cfg->wake_gpio_num = DEFAULT_WAKE_GPIO_NUM;
    cfg->wake_gpio_active_low = DEFAULT_WAKE_GPIO_ACTIVE_LOW;
    cfg->wake_gpio_poll_ms = DEFAULT_WAKE_GPIO_POLL_MS;
    cfg->wake_gpio_debounce_ms = DEFAULT_WAKE_GPIO_DEBOUNCE_MS;
    strscpy(cfg->timezone, DEFAULT_TIMEZONE, sizeof(cfg->timezone));
    strscpy(cfg->terminal_security_level, DEFAULT_TERMINAL_SECURITY_LEVEL, sizeof(cfg->terminal_security_level));
    strscpy(cfg->web_default_pet_package_id, DEFAULT_WEB_PET_PACKAGE_ID, sizeof(cfg->web_default_pet_package_id));
    strscpy(cfg->provider_model, DEFAULT_LLM_MODEL, sizeof(cfg->provider_model));
}

/** 钳制整型配置值到 [min_value, max_value] 范围内，超出返回 fallback。 */
int runtime_config_clamp_int(int value, int min_value, int max_value, int fallback)
{
    if (value < min_value || value > max_value) {
        return fallback;
    }
    return value;
}

const char *runtime_config_get_timezone(void)
{
    return s_cfg.timezone[0] ? s_cfg.timezone : runtime_config_default_timezone();
}

int runtime_config_get_web_port(void)
{
    return s_cfg.web_port;
}

int runtime_config_get_session_max_msgs(void)
{
    return s_cfg.session_max_msgs;
}

const char *runtime_config_get_web_default_pet_package_id(void)
{
    return s_cfg.web_default_pet_package_id[0]
        ? s_cfg.web_default_pet_package_id
        : runtime_config_default_web_pet_package_id();
}

const char *runtime_config_get_terminal_security_level(void)
{
    return (strcmp(s_cfg.terminal_security_level, "plan") == 0 ||
            strcmp(s_cfg.terminal_security_level, "build") == 0)
        ? s_cfg.terminal_security_level
        : runtime_config_default_terminal_security_level();
}

const char *runtime_config_get_active_provider(void)
{
    return s_cfg.active_provider;
}

const char *runtime_config_get_active_provider_name(void)
{
    return runtime_config_get_active_provider();
}

const char *runtime_config_get_provider_api_key(void)
{
    return s_cfg.provider_api_key;
}

const char *runtime_config_get_provider_model(void)
{
    return s_cfg.provider_model[0] ? s_cfg.provider_model : runtime_config_default_llm_model();
}

/** 根据 provider 名称查找其 model 字段（从 providers[] 数组中查找）。 */
const char *runtime_config_get_provider_model_for_name(const char *provider_name)
{
    if (!provider_name || !provider_name[0]) {
        return NULL;
    }
    for (int i = 0; i < s_cfg.provider_count; i++) {
        if (strcmp(s_cfg.providers[i].name, provider_name) == 0) {
            return s_cfg.providers[i].model[0] ? s_cfg.providers[i].model : NULL;
        }
    }
    return NULL;
}

int runtime_config_get_provider_count(void)
{
    return s_cfg.provider_count;
}

const char *runtime_config_get_provider_name_at(int index)
{
    if (index < 0 || index >= s_cfg.provider_count) {
        return NULL;
    }
    return s_cfg.providers[index].name[0] ? s_cfg.providers[index].name : NULL;
}

const char *runtime_config_get_provider_openai_base_url(void)
{
    return s_cfg.provider_openai_base_url;
}

const char *runtime_config_get_provider_api_mode(void)
{
    return s_cfg.provider_api_mode;
}

const char *runtime_config_get_provider_thinking_mode(void)
{
    return s_cfg.provider_thinking_mode;
}

const char *runtime_config_get_provider_reasoning_effort(void)
{
    return s_cfg.provider_reasoning_effort;
}

bool runtime_config_provider_needs_reasoning_content(void)
{
    return s_cfg.provider_needs_reasoning_content;
}

/** 获取上下文限制 token 数：provider 级优先 → common 级 → 0。 */
int runtime_config_get_context_limit_tokens(void)
{
    if (s_cfg.provider_context_limit_tokens > 0) {
        return s_cfg.provider_context_limit_tokens;
    }
    return s_cfg.common_context_limit_tokens;
}

/** 获取最大输出 token 数：provider 级 → common 级 → LLM_MAX_TOKENS。 */
int runtime_config_get_max_output_tokens(void)
{
    if (s_cfg.provider_max_output_tokens > 0) {
        return s_cfg.provider_max_output_tokens;
    }
    if (s_cfg.common_max_output_tokens > 0) {
        return s_cfg.common_max_output_tokens;
    }
    return LLM_MAX_TOKENS;
}

/** 获取请求超时：provider 级 → DEFAULT_LLM_REQUEST_TIMEOUT_MS(300s)。 */
int runtime_config_get_request_timeout_ms(void)
{
    if (s_cfg.provider_request_timeout_ms > 0) {
        return s_cfg.provider_request_timeout_ms;
    }
    return runtime_config_default_request_timeout_ms();
}

const char *runtime_config_get_feishu_app_id(void)
{
    return s_cfg.feishu_app_id;
}

const char *runtime_config_get_feishu_app_secret(void)
{
    return s_cfg.feishu_app_secret;
}

const char *runtime_config_get_feishu_default_chat_id(void)
{
    return s_cfg.feishu_default_chat_id;
}

const char *runtime_config_get_bigmodel_api_key(void)
{
    return s_cfg.bigmodel_api_key;
}

int runtime_config_get_audio_ai_vol(void)
{
    return s_cfg.audio_ai_vol;
}

int runtime_config_get_audio_ai_gain(void)
{
    return s_cfg.audio_ai_gain;
}

int runtime_config_get_audio_ao_vol(void)
{
    return s_cfg.audio_ao_vol;
}

int runtime_config_get_audio_ao_gain(void)
{
    return s_cfg.audio_ao_gain;
}

int runtime_config_get_voice_record_ms(void)
{
    return s_cfg.voice_record_ms;
}

int runtime_config_get_compress_trigger_msgs(void)
{
    return s_cfg.compress_trigger_msgs;
}

int runtime_config_get_compress_keep_msgs(void)
{
    return s_cfg.compress_keep_msgs;
}

bool runtime_config_get_learning_review_enabled(void)
{
    return s_cfg.learning_review_enabled;
}

int runtime_config_get_cron_check_interval_ms(void)
{
    return s_cfg.cron_check_interval_ms;
}

int runtime_config_get_heartbeat_interval_ms(void)
{
    return s_cfg.heartbeat_interval_ms;
}

int runtime_config_get_wake_gpio_num(void)
{
    return s_cfg.wake_gpio_num;
}

int runtime_config_get_wake_gpio_active_low(void)
{
    return s_cfg.wake_gpio_active_low;
}

int runtime_config_get_wake_gpio_poll_ms(void)
{
    return s_cfg.wake_gpio_poll_ms;
}

int runtime_config_get_wake_gpio_debounce_ms(void)
{
    return s_cfg.wake_gpio_debounce_ms;
}
