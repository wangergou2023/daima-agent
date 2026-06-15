#include "runtime.h"
#include "paths.h"
#include "runtime_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cJSON.h"
#include "autoconf.h"
#include "linux/printk.h"
#include "linux/slab.h"
#include "linux/kernel.h"
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

static runtime_config_state_t s_cfg;

static void reset_defaults(void)
{
    memset(&s_cfg, 0, sizeof(s_cfg));
    s_cfg.web_port = DEFAULT_WEB_PORT;
    s_cfg.session_max_msgs = SESSION_MAX_MSGS;
    s_cfg.compress_trigger_msgs = DEFAULT_COMPRESS_TRIGGER_MSGS;
    s_cfg.compress_keep_msgs = DEFAULT_COMPRESS_KEEP_MSGS;
    s_cfg.learning_review_enabled = false;
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
    strscpy(s_cfg.timezone, DEFAULT_TIMEZONE, sizeof(s_cfg.timezone));
    strscpy(s_cfg.terminal_security_level, DEFAULT_TERMINAL_SECURITY_LEVEL, sizeof(s_cfg.terminal_security_level));
    strscpy(s_cfg.web_default_pet_package_id, DEFAULT_WEB_PET_PACKAGE_ID, sizeof(s_cfg.web_default_pet_package_id));
    strscpy(s_cfg.provider_model, DEFAULT_LLM_MODEL, sizeof(s_cfg.provider_model));
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
    FILE *f = fopen(path_runtime_config_file(), "rb");
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

    buf = kzalloc((size_t)size + 1, GFP_KERNEL);
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
    strscpy(out, item->valuestring, out_size);
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

err_t runtime_config_init(void)
{
    char *text = NULL;
    cJSON *root = NULL;

    reset_defaults();

    if (access(path_runtime_config_file(), F_OK) != 0) {
        pr_warn("Runtime config missing: %s", path_runtime_config_file());
        pr_warn("Please create it with reference to: %s/config.example.json", path_config_dir());
        s_cfg.loaded = 1;
        return 0;
    }

    text = read_config_text();
    if (!text) {
        pr_warn("Cannot read runtime config: %s", path_runtime_config_file());
        s_cfg.loaded = 1;
        return 0;
    }

    root = cJSON_Parse(text);
    kfree(text);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        pr_warn("Invalid runtime config JSON: %s", path_runtime_config_file());
        s_cfg.loaded = 1;
        return 0;
    }

    runtime_config_apply_values(&s_cfg, root);
    cJSON_Delete(root);
    s_cfg.loaded = 1;

    pr_info("Runtime config loaded: %s%s%s", path_runtime_config_file(), s_cfg.active_provider[0] ? " active_provider=" : "", s_cfg.active_provider[0] ? s_cfg.active_provider : "");
    return 0;
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

static bool terminal_security_level_valid(const char *level)
{
    return level &&
           (strcmp(level, "plan") == 0 ||
            strcmp(level, "build") == 0);
}

const char *runtime_config_get_terminal_security_level(void)
{
    return terminal_security_level_valid(s_cfg.terminal_security_level)
        ? s_cfg.terminal_security_level
        : DEFAULT_TERMINAL_SECURITY_LEVEL;
}

static err_t write_config_json_atomic(cJSON *root)
{
    if (!root) {
        return ERR_INVALID_ARG;
    }

    char *text = cJSON_Print(root);
    if (!text) {
        return ERR_NO_MEM;
    }

    char tmp_path[1024];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path_runtime_config_file());
    FILE *f = fopen(tmp_path, "wb");
    if (!f) {
        kfree(text);
        return ERR_FAIL;
    }

    size_t len = strlen(text);
    bool ok = fwrite(text, 1, len, f) == len && fwrite("\n", 1, 1, f) == 1;
    if (fclose(f) != 0) {
        ok = false;
    }
    kfree(text);
    if (!ok) {
        unlink(tmp_path);
        return ERR_FAIL;
    }
    if (rename(tmp_path, path_runtime_config_file()) != 0) {
        unlink(tmp_path);
        return ERR_FAIL;
    }
    return 0;
}

err_t runtime_config_set_terminal_security_level(const char *level)
{
    if (!terminal_security_level_valid(level)) {
        return ERR_INVALID_ARG;
    }

    char *text = read_config_text();
    cJSON *root = text ? cJSON_Parse(text) : NULL;
    kfree(text);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        root = cJSON_CreateObject();
    }
    if (!root) {
        return ERR_NO_MEM;
    }

    cJSON *common = cJSON_GetObjectItemCaseSensitive(root, "common");
    if (!common || !cJSON_IsObject(common)) {
        cJSON_DeleteItemFromObjectCaseSensitive(root, "common");
        common = cJSON_AddObjectToObject(root, "common");
    }
    if (!common) {
        cJSON_Delete(root);
        return ERR_NO_MEM;
    }
    cJSON_DeleteItemFromObjectCaseSensitive(common, "terminal_security_level");
    cJSON_AddStringToObject(common, "terminal_security_level", level);

    err_t err = write_config_json_atomic(root);
    cJSON_Delete(root);
    if (err != 0) {
        return err;
    }

    strscpy(s_cfg.terminal_security_level, level, sizeof(s_cfg.terminal_security_level));
    pr_info("Terminal security level set to %s", level);
    return 0;
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
    return s_cfg.provider_model[0] ? s_cfg.provider_model : DEFAULT_LLM_MODEL;
}

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

int runtime_config_get_context_limit_tokens(void)
{
    if (s_cfg.provider_context_limit_tokens > 0) {
        return s_cfg.provider_context_limit_tokens;
    }
    return s_cfg.common_context_limit_tokens;
}

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

int runtime_config_get_request_timeout_ms(void)
{
    if (s_cfg.provider_request_timeout_ms > 0) {
        return s_cfg.provider_request_timeout_ms;
    }
    return DEFAULT_LLM_REQUEST_TIMEOUT_MS;
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
