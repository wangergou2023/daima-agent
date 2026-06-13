#pragma once

#include "core/agent_turn_common.h"
#include "core/err.h"
#include "cJSON.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    bool enabled;
    const char *name;
} agent_extension_t;

typedef daima_err_t (*agent_hook_on_intent_fn)(daima_msg_t *msg);
typedef daima_err_t (*agent_hook_on_prepare_fn)(daima_msg_t *msg,
    char *system_prompt, size_t system_prompt_size, cJSON *messages);
typedef daima_err_t (*agent_hook_before_run_fn)(daima_msg_t *msg,
    const char **model_override, const char *tools_json);
typedef daima_err_t (*agent_hook_replace_run_fn)(daima_msg_t *msg,
    char *system_prompt, cJSON *messages, const char *tools_json,
    char **out_final_text);
typedef void (*agent_hook_on_finish_fn)(daima_msg_t *msg, const char *response);

typedef struct {
    const char *name;
    agent_hook_on_intent_fn   on_intent;
    agent_hook_on_prepare_fn  on_prepare;
    agent_hook_before_run_fn  before_run;
    agent_hook_replace_run_fn replace_run;
    agent_hook_on_finish_fn   on_finish;
    bool enabled;
} agent_extension_hooks_t;

#define AGENT_MAX_EXTENSIONS 16

void agent_hooks_init(void);
void agent_hooks_register(agent_extension_hooks_t *hooks);

daima_err_t agent_hooks_trigger_intent(daima_msg_t *msg);
daima_err_t agent_hooks_trigger_prepare(daima_msg_t *msg,
    char *system_prompt, size_t system_prompt_size, cJSON *messages);
daima_err_t agent_hooks_trigger_before_run(daima_msg_t *msg,
    const char **model_override, const char *tools_json);
daima_err_t agent_hooks_trigger_replace_run(daima_msg_t *msg,
    char *system_prompt, cJSON *messages, const char *tools_json,
    char **out_final_text);
void agent_hooks_trigger_finish(daima_msg_t *msg, const char *response);
