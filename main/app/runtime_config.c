#include "app/runtime_config.h"
#include "app/daima_paths.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cJSON.h"
#include "daima_config.h"
#include "daima_log.h"

static const char *TAG = "runtime_config";

#define PROVIDER_NAME_MAX 64
#define STRING_SMALL_MAX  32
#define API_KEY_MAX       320
#define URL_MAX           256
#define MODEL_MAX         64
#define PET_PACKAGE_ID_MAX 128
#define FEISHU_APP_ID_MAX 64
#define FEISHU_SECRET_MAX 128
#define FEISHU_CHAT_ID_MAX 64
#define BIGMODEL_KEY_MAX  128

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

typedef struct {
    int loaded;
    int web_port;
    int session_max_msgs;
    int common_context_limit_tokens;
    int compress_trigger_msgs;
    int compress_keep_msgs;
    int cron_check_interval_ms;
    int heartbeat_interval_ms;
    char timezone[STRING_SMALL_MAX];
    char web_default_pet_package_id[PET_PACKAGE_ID_MAX];

    char active_provider[PROVIDER_NAME_MAX];
    char provider_api_key[API_KEY_MAX];
    char provider_model[MODEL_MAX];
    char provider_openai_base_url[URL_MAX];
    char provider_thinking_mode[STRING_SMALL_MAX];
    int provider_context_limit_tokens;
    bool provider_needs_reasoning_content;

    char feishu_app_id[FEISHU_APP_ID_MAX];
    char feishu_app_secret[FEISHU_SECRET_MAX];
    char feishu_default_chat_id[FEISHU_CHAT_ID_MAX];

    char bigmodel_api_key[BIGMODEL_KEY_MAX];
    int audio_ai_vol;
    int audio_ai_gain;
    int audio_ao_vol;
    int audio_ao_gain;
    int voice_record_ms;

    int wake_gpio_num;
    int wake_gpio_active_low;
    int wake_gpio_poll_ms;
    int wake_gpio_debounce_ms;
} runtime_config_state_t;

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

static int clamp_int(int value, int min_value, int max_value, int fallback)
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

static const cJSON *get_object_item(const cJSON *root, const char *key)
{
    if (!root || !cJSON_IsObject(root)) {
        return NULL;
    }
    return cJSON_GetObjectItemCaseSensitive((cJSON *)root, key);
}

static bool json_copy_string(const cJSON *root, const char *key, char *out, size_t out_size)
{
    const cJSON *item = get_object_item(root, key);
    if (!item || !cJSON_IsString(item) || !item->valuestring || !item->valuestring[0]) {
        return false;
    }
    snprintf(out, out_size, "%s", item->valuestring);
    return true;
}

static bool json_read_int(const cJSON *root, const char *key, int *out)
{
    const cJSON *item = get_object_item(root, key);
    if (!item || !cJSON_IsNumber(item) || !out) {
        return false;
    }
    *out = (int)item->valuedouble;
    return true;
}

static bool json_read_bool(const cJSON *root, const char *key, bool *out)
{
    const cJSON *item = get_object_item(root, key);
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

static void apply_common_values(const cJSON *common)
{
    int value = 0;

    if (!common || !cJSON_IsObject(common)) {
        return;
    }

    if (json_read_int(common, "web_port", &value)) {
        s_cfg.web_port = clamp_int(value, 1, 65535, s_cfg.web_port);
    }
    if (json_read_int(common, "session_max_msgs", &value)) {
        s_cfg.session_max_msgs = clamp_int(value, 8, DAIMA_SESSION_MAX_MSGS, s_cfg.session_max_msgs);
    }
    if (json_read_int(common, "context_limit_tokens", &value) && value >= 1 && value <= 2000000) {
        s_cfg.common_context_limit_tokens = value;
    }
    if (json_read_int(common, "compress_trigger_msgs", &value)) {
        s_cfg.compress_trigger_msgs = clamp_int(value, 4, 10000, s_cfg.compress_trigger_msgs);
    }
    if (json_read_int(common, "compress_keep_msgs", &value)) {
        s_cfg.compress_keep_msgs = clamp_int(value, 2, 1000, s_cfg.compress_keep_msgs);
    }
    if (json_read_int(common, "cron_check_interval_ms", &value)) {
        s_cfg.cron_check_interval_ms = clamp_int(value, 1000, 86400000, s_cfg.cron_check_interval_ms);
    }
    if (json_read_int(common, "heartbeat_interval_ms", &value)) {
        s_cfg.heartbeat_interval_ms = clamp_int(value, 1000, 86400000, s_cfg.heartbeat_interval_ms);
    }
    json_copy_string(common, "timezone", s_cfg.timezone, sizeof(s_cfg.timezone));
}

static void apply_web_values(const cJSON *root)
{
    if (!root || !cJSON_IsObject(root)) {
        return;
    }

    json_copy_string(root,
                     "default_pet_package_id",
                     s_cfg.web_default_pet_package_id,
                     sizeof(s_cfg.web_default_pet_package_id));
}

static void apply_provider_values(const char *provider_name, const cJSON *provider)
{
    bool bool_value = false;
    int int_value = 0;

    if (!provider_name || !provider_name[0] || !provider || !cJSON_IsObject(provider)) {
        return;
    }

    snprintf(s_cfg.active_provider, sizeof(s_cfg.active_provider), "%s", provider_name);
    json_copy_string(provider, "api_key", s_cfg.provider_api_key, sizeof(s_cfg.provider_api_key));
    json_copy_string(provider, "model", s_cfg.provider_model, sizeof(s_cfg.provider_model));
    json_copy_string(provider, "openai_base_url", s_cfg.provider_openai_base_url, sizeof(s_cfg.provider_openai_base_url));
    json_copy_string(provider, "thinking_mode", s_cfg.provider_thinking_mode, sizeof(s_cfg.provider_thinking_mode));
    if (json_read_int(provider, "context_limit_tokens", &int_value) && int_value >= 1 && int_value <= 2000000) {
        s_cfg.provider_context_limit_tokens = int_value;
    }
    if (json_read_bool(provider, "needs_reasoning_content", &bool_value)) {
        s_cfg.provider_needs_reasoning_content = bool_value;
    }

    DAIMA_LOGI(TAG, "Runtime config applied provider: %s", provider_name);
}

static void apply_feishu_values(const cJSON *root)
{
    if (!root || !cJSON_IsObject(root)) {
        return;
    }
    json_copy_string(root, "app_id", s_cfg.feishu_app_id, sizeof(s_cfg.feishu_app_id));
    json_copy_string(root, "app_secret", s_cfg.feishu_app_secret, sizeof(s_cfg.feishu_app_secret));
    json_copy_string(root, "default_chat_id", s_cfg.feishu_default_chat_id, sizeof(s_cfg.feishu_default_chat_id));
}

static void apply_audio_values(const cJSON *root)
{
    int value = 0;

    if (!root || !cJSON_IsObject(root)) {
        return;
    }

    json_copy_string(root, "bigmodel_api_key", s_cfg.bigmodel_api_key, sizeof(s_cfg.bigmodel_api_key));
    if (json_read_int(root, "ai_vol", &value)) {
        s_cfg.audio_ai_vol = clamp_int(value, 0, 120, s_cfg.audio_ai_vol);
    }
    if (json_read_int(root, "ai_gain", &value)) {
        s_cfg.audio_ai_gain = clamp_int(value, 0, 120, s_cfg.audio_ai_gain);
    }
    if (json_read_int(root, "ao_vol", &value)) {
        s_cfg.audio_ao_vol = clamp_int(value, 0, 120, s_cfg.audio_ao_vol);
    }
    if (json_read_int(root, "ao_gain", &value)) {
        s_cfg.audio_ao_gain = clamp_int(value, 0, 120, s_cfg.audio_ao_gain);
    }
    if (json_read_int(root, "voice_record_ms", &value)) {
        s_cfg.voice_record_ms = clamp_int(value, 500, 600000, s_cfg.voice_record_ms);
    }
}

static void apply_mips_values(const cJSON *root)
{
    int value = 0;

    if (!root || !cJSON_IsObject(root)) {
        return;
    }

    if (json_read_int(root, "wake_gpio_num", &value)) {
        s_cfg.wake_gpio_num = clamp_int(value, 0, 1024, s_cfg.wake_gpio_num);
    }
    if (json_read_int(root, "wake_gpio_active_low", &value)) {
        s_cfg.wake_gpio_active_low = clamp_int(value, 0, 1, s_cfg.wake_gpio_active_low);
    }
    if (json_read_int(root, "wake_gpio_poll_ms", &value)) {
        s_cfg.wake_gpio_poll_ms = clamp_int(value, 1, 60000, s_cfg.wake_gpio_poll_ms);
    }
    if (json_read_int(root, "wake_gpio_debounce_ms", &value)) {
        s_cfg.wake_gpio_debounce_ms = clamp_int(value, 0, 60000, s_cfg.wake_gpio_debounce_ms);
    }
}

static void apply_active_provider(const cJSON *providers, const char *active_provider)
{
    const cJSON *selected = NULL;

    if (!providers || !cJSON_IsObject(providers) || !active_provider || !active_provider[0]) {
        DAIMA_LOGW(TAG, "Runtime config missing active_provider");
        return;
    }

    selected = get_object_item(providers, active_provider);
    if (!selected || !cJSON_IsObject(selected)) {
        DAIMA_LOGW(TAG, "Runtime config provider not found: %s", active_provider);
        return;
    }

    apply_provider_values(active_provider, selected);
}

static void apply_runtime_values(const cJSON *root)
{
    const cJSON *common = NULL;
    const cJSON *providers = NULL;
    const cJSON *feishu = NULL;
    const cJSON *audio = NULL;
    const cJSON *mips = NULL;
    const cJSON *web = NULL;
    const char *active_provider = NULL;

    if (!root || !cJSON_IsObject(root)) {
        return;
    }

    common = get_object_item(root, "common");
    providers = get_object_item(root, "providers");
    feishu = get_object_item(root, "feishu");
    audio = get_object_item(root, "audio");
    mips = get_object_item(root, "mips");
    web = get_object_item(root, "web");
    active_provider = cJSON_GetStringValue((cJSON *)get_object_item(root, "active_provider"));

    apply_feishu_values(feishu);
    apply_audio_values(audio);
    apply_mips_values(mips);
    apply_web_values(web);
    apply_common_values(common);

    if (providers && cJSON_IsObject(providers)) {
        apply_active_provider(providers, active_provider);
    }
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

    apply_runtime_values(root);
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
