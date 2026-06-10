#pragma once

#include <stdbool.h>

#include "daima_err.h"

daima_err_t runtime_config_init(void);

const char *runtime_config_get_timezone(void);
int runtime_config_get_web_port(void);
int runtime_config_get_session_max_msgs(void);
const char *runtime_config_get_web_default_pet_package_id(void);
const char *runtime_config_get_terminal_security_level(void);
daima_err_t runtime_config_set_terminal_security_level(const char *level);

const char *runtime_config_get_active_provider(void);
const char *runtime_config_get_provider_api_key(void);
const char *runtime_config_get_provider_model(void);
const char *runtime_config_get_provider_openai_base_url(void);
const char *runtime_config_get_provider_api_mode(void);
const char *runtime_config_get_provider_thinking_mode(void);
const char *runtime_config_get_provider_reasoning_effort(void);
bool runtime_config_provider_needs_reasoning_content(void);
int runtime_config_get_context_limit_tokens(void);
int runtime_config_get_max_output_tokens(void);
int runtime_config_get_request_timeout_ms(void);

const char *runtime_config_get_feishu_app_id(void);
const char *runtime_config_get_feishu_app_secret(void);
const char *runtime_config_get_feishu_default_chat_id(void);

const char *runtime_config_get_bigmodel_api_key(void);
int runtime_config_get_audio_ai_vol(void);
int runtime_config_get_audio_ai_gain(void);
int runtime_config_get_audio_ao_vol(void);
int runtime_config_get_audio_ao_gain(void);
int runtime_config_get_voice_record_ms(void);

int runtime_config_get_compress_trigger_msgs(void);
int runtime_config_get_compress_keep_msgs(void);
bool runtime_config_get_learning_review_enabled(void);
int runtime_config_get_cron_check_interval_ms(void);
int runtime_config_get_heartbeat_interval_ms(void);

int runtime_config_get_wake_gpio_num(void);
int runtime_config_get_wake_gpio_active_low(void);
int runtime_config_get_wake_gpio_poll_ms(void);
int runtime_config_get_wake_gpio_debounce_ms(void);
