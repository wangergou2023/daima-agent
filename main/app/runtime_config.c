#include "app/runtime_config.h"
#include "app/daima_paths.h"
#include "app/runtime_config_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cJSON.h"
#include "daima_config.h"
#include "daima_log.h"

static const char *TAG = "runtime_config";

static const char *DEFAULT_TIMEZONE = "CST-8";
static const char *DEFAULT_LLM_MODEL = "kimi-k2.5";
static const char *DEFAULT_WEB_PET_PACKAGE_ID = "guga.codex-pet";

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
};

static runtime_config_state_t s_cfg;

static void reset_defaults(void)
{
    memset(&s_cfg, 0, sizeof(s_cfg));
    s_cfg.web_port = DEFAULT_WEB_PORT;
    s_cfg.session_max_msgs = DAIMA_SESSION_MAX_MSGS;
    s_cfg.compress_trigger_msgs = DEFAULT_COMPRESS_TRIGGER_MSGS;
    s_cfg.compress_keep_msgs = DEFAULT_COMPRESS_KEEP_MSGS;
    s_cfg.cron_check_interval_ms = DEFAULT_CRON_CHECK_INTERVAL_MS;
    s_cfg.heartbeat_interval_ms = DEFAULT_HEARTBEAT_INTERVAL_MS;
    s_cfg.audio_ai_vol = DEFAULT_AUDIO_AI_VOL;
    s_cfg.audio_ai_gain = DEFAULT_AUDIO_AI_GAIN;
    s_cfg.audio_ao_vol = DEFAULT_AUDIO_AO_VOL;
    s_cfg.audio_ao_gain = DEFAULT_AUDIO_AO_GAIN;
    s_cfg.voice_record_ms = DEFAULT_VOICE_RECORD_MS;
    s_cfg.wake_gpio_num = DEFAULT_WAKE_GPIO_NUM;
    s_cfg.wake_gpio_active_low = DEFAULT_WAKE_GPIO_ACTIVE_LOW;
    s_cfg.wake_gpio_poll_ms = DEFAULT_WAKE_GPIO_POLL_MS;
    s_cfg.wake_gpio_debounce_ms = DEFAULT_WAKE_GPIO_DEBOUNCE_MS;
    snprintf(s_cfg.timezone, sizeof(s_cfg.timezone), "%s", DEFAULT_TIMEZONE);
    snprintf(s_cfg.web_default_pet_package_id, sizeof(s_cfg.web_default_pet_package_id),
             "%s", DEFAULT_WEB_PET_PACKAGE_ID);
    snprintf(s_cfg.provider_model, sizeof(s_cfg.provider_model), "%s", DEFAULT_LLM_MODEL);
}

int runtime_config_clamp_int(int value, int min_value, int max_value, int fallback)
{
    if (value < min_value || value > max_value) {
        return fallback;
    }
    return value;
}

static char *read_config_text(void)
{
    FILE *f = fopen(daima_path_runtime_config_file(), "rb");
    char *buf = NULL;
    long size = 0;
    size_t n = 0;

    if (!f) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    size = ftell(f);
    if (size < 0 || size > 128 * 1024) {
        fclose(f);
        return NULL;
    }
    rewind(f);

    buf = calloc(1, (size_t)size + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }

    n = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[n] = '\0';
    return buf;
}

const cJSON *runtime_config_get_object_item(const cJSON *root, const char *key)
{
    if (!root || !cJSON_IsObject(root)) {
        return NULL;
    }
    return cJSON_GetObjectItemCaseSensitive((cJSON *)root, key);
}

bool runtime_config_json_copy_string(const cJSON *root, const char *key, char *out, size_t out_size)
{
    const cJSON *item = runtime_config_get_object_item(root, key);
    if (!item || !cJSON_IsString(item) || !item->valuestring || !item->valuestring[0]) {
        return false;
    }
    snprintf(out, out_size, "%s", item->valuestring);
    return true;
}

bool runtime_config_json_read_int(const cJSON *root, const char *key, int *out)
{
    const cJSON *item = runtime_config_get_object_item(root, key);
    if (!item || !cJSON_IsNumber(item) || !out) {
        return false;
    }
    *out = (int)item->valuedouble;
    return true;
}

bool runtime_config_json_read_bool(const cJSON *root, const char *key, bool *out)
{
    const cJSON *item = runtime_config_get_object_item(root, key);
    if (!item || !out) {
        return false;
    }
    if (cJSON_IsBool(item)) {
        *out = cJSON_IsTrue(item);
        return true;
    }
    if (cJSON_IsNumber(item)) {
        *out = item->valuedouble != 0;
        return true;
    }
    return false;
}

daima_err_t runtime_config_init(void)
{
    char *text = NULL;
    cJSON *root = NULL;

    reset_defaults();

    if (access(daima_path_runtime_config_file(), F_OK) != 0) {
        DAIMA_LOGW(TAG, "Runtime config missing: %s", daima_path_runtime_config_file());
        DAIMA_LOGW(TAG, "Please create it with reference to: %s/config.example.json", daima_path_config_dir());
        s_cfg.loaded = 1;
        return DAIMA_OK;
    }

    text = read_config_text();
    if (!text) {
        DAIMA_LOGW(TAG, "Cannot read runtime config: %s", daima_path_runtime_config_file());
        s_cfg.loaded = 1;
        return DAIMA_OK;
    }

    root = cJSON_Parse(text);
    free(text);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        DAIMA_LOGW(TAG, "Invalid runtime config JSON: %s", daima_path_runtime_config_file());
        s_cfg.loaded = 1;
        return DAIMA_OK;
    }

    runtime_config_apply_values(&s_cfg, root);
    cJSON_Delete(root);
    s_cfg.loaded = 1;

    DAIMA_LOGI(TAG, "Runtime config loaded: %s%s%s",
               daima_path_runtime_config_file(),
               s_cfg.active_provider[0] ? " active_provider=" : "",
               s_cfg.active_provider[0] ? s_cfg.active_provider : "");
    return DAIMA_OK;
}

const char *runtime_config_get_timezone(void)
{
    return s_cfg.timezone[0] ? s_cfg.timezone : DEFAULT_TIMEZONE;
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
        : DEFAULT_WEB_PET_PACKAGE_ID;
}

const char *runtime_config_get_active_provider(void)
{
    return s_cfg.active_provider;
}

const char *runtime_config_get_provider_api_key(void)
{
    return s_cfg.provider_api_key;
}

const char *runtime_config_get_provider_model(void)
{
    return s_cfg.provider_model[0] ? s_cfg.provider_model : DEFAULT_LLM_MODEL;
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

bool runtime_config_provider_needs_reasoning_content(void)
{
    return s_cfg.provider_needs_reasoning_content;
}

int runtime_config_get_context_limit_tokens(void)
{
    if (s_cfg.provider_context_limit_tokens > 0) {
        return s_cfg.provider_context_limit_tokens;
    }
    return s_cfg.common_context_limit_tokens;
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
