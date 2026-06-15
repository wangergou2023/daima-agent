#pragma once

#include "turn_common.h"
#include "err.h"
#include "cJSON.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    bool enabled;
    const char *name;
} agent_extension_t;

typedef err_t (*agent_hook_on_intent_fn)(struct message *msg);
typedef err_t (*agent_hook_on_prepare_fn)(struct message *msg,
    char *system_prompt, size_t system_prompt_size, cJSON *messages);
typedef err_t (*agent_hook_before_run_fn)(struct message *msg,
    const char **model_override, const char *tools_json);
typedef err_t (*agent_hook_replace_run_fn)(struct message *msg,
    char *system_prompt, cJSON *messages, const char *tools_json,
    char **out_final_text);
typedef void (*agent_hook_on_finish_fn)(struct message *msg, const char *response);

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

err_t agent_hooks_trigger_intent(struct message *msg);
err_t agent_hooks_trigger_prepare(struct message *msg,
    char *system_prompt, size_t system_prompt_size, cJSON *messages);
err_t agent_hooks_trigger_before_run(struct message *msg,
    const char **model_override, const char *tools_json);
err_t agent_hooks_trigger_replace_run(struct message *msg,
    char *system_prompt, cJSON *messages, const char *tools_json,
    char **out_final_text);
void agent_hooks_trigger_finish(struct message *msg, const char *response);
