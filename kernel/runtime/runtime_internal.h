/* 运行时配置内部结构：定义 runtime_config_state_t 结构体和辅助函数。
 * 由 runtime.c 和 runtime_sections.c 共享，不对外暴露。 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "cjson.h"

/* 字段最大长度常量 */
#define RUNTIME_PROVIDER_NAME_MAX 64
#define RUNTIME_STRING_SMALL_MAX  32
#define RUNTIME_API_KEY_MAX       320
#define RUNTIME_URL_MAX           256
#define RUNTIME_MODEL_MAX         64
#define RUNTIME_PET_PACKAGE_ID_MAX 128
#define RUNTIME_FEISHU_APP_ID_MAX 64
#define RUNTIME_FEISHU_SECRET_MAX 128
#define RUNTIME_FEISHU_CHAT_ID_MAX 64
#define RUNTIME_BIGMODEL_KEY_MAX  128
#define RUNTIME_PROVIDER_MAX      16

/* Provider 条目：名称 + 模型标识 */
typedef struct {
    char name[RUNTIME_PROVIDER_NAME_MAX];
    char model[RUNTIME_MODEL_MAX];
    char api_key[RUNTIME_API_KEY_MAX];
    char openai_base_url[RUNTIME_URL_MAX];
    char api_mode[RUNTIME_STRING_SMALL_MAX];
    char thinking_mode[RUNTIME_STRING_SMALL_MAX];
    char reasoning_effort[RUNTIME_STRING_SMALL_MAX];
    int context_limit_tokens;
    int max_output_tokens;
    int request_timeout_ms;
    bool needs_reasoning_content;
} runtime_provider_entry_t;

/* 运行时配置全量状态结构体 */
typedef struct {
    int loaded;
    int web_port;
    int session_max_msgs;
    int common_context_limit_tokens;
    int common_max_output_tokens;
    int compress_trigger_msgs;
    int compress_keep_msgs;
    bool learning_review_enabled;
    int cron_check_interval_ms;
    int heartbeat_interval_ms;
    char timezone[RUNTIME_STRING_SMALL_MAX];
    char terminal_security_level[RUNTIME_STRING_SMALL_MAX];
    char web_default_pet_package_id[RUNTIME_PET_PACKAGE_ID_MAX];

    char active_provider[RUNTIME_PROVIDER_NAME_MAX];
    char provider_api_key[RUNTIME_API_KEY_MAX];
    char provider_model[RUNTIME_MODEL_MAX];
    char provider_openai_base_url[RUNTIME_URL_MAX];
    char provider_api_mode[RUNTIME_STRING_SMALL_MAX];
    char provider_thinking_mode[RUNTIME_STRING_SMALL_MAX];
    char provider_reasoning_effort[RUNTIME_STRING_SMALL_MAX];
    int provider_context_limit_tokens;
    int provider_max_output_tokens;
    int provider_request_timeout_ms;
    bool provider_needs_reasoning_content;
    runtime_provider_entry_t providers[RUNTIME_PROVIDER_MAX];
    int provider_count;

    char feishu_app_id[RUNTIME_FEISHU_APP_ID_MAX];
    char feishu_app_secret[RUNTIME_FEISHU_SECRET_MAX];
    char feishu_default_chat_id[RUNTIME_FEISHU_CHAT_ID_MAX];

    char bigmodel_api_key[RUNTIME_BIGMODEL_KEY_MAX];
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

int runtime_config_clamp_int(int value, int min_value, int max_value, int fallback);
const cJSON *runtime_config_get_object_item(const cJSON *root, const char *key);
bool runtime_config_json_copy_string(const cJSON *root, const char *key, char *out, size_t out_size);
bool runtime_config_json_read_int(const cJSON *root, const char *key, int *out);
bool runtime_config_json_read_bool(const cJSON *root, const char *key, bool *out);

void runtime_config_apply_values(runtime_config_state_t *cfg, const cJSON *root);
