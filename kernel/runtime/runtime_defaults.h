/* 运行时配置默认值与共享状态。 */

#pragma once

#include "runtime_internal.h"

extern runtime_config_state_t s_cfg;

const char *runtime_config_default_timezone(void);
const char *runtime_config_default_llm_model(void);
const char *runtime_config_default_web_pet_package_id(void);
const char *runtime_config_default_terminal_security_level(void);
int runtime_config_default_request_timeout_ms(void);

void runtime_config_reset_defaults(runtime_config_state_t *cfg);
