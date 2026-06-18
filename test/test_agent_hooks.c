#include "hooks.h"

#include <assert.h>
#include <errno.h>
#include <string.h>

static int intent_calls;
static int prepare_calls;
static int before_calls;
static int finish_calls;
static int replace_a_calls;
static int replace_b_calls;
static const char *seen_model;

static int on_intent_a(struct message *msg)
{
    assert(msg != NULL);
    intent_calls = intent_calls * 10 + 1;
    return 0;
}

static int on_intent_b(struct message *msg)
{
    assert(msg != NULL);
    intent_calls = intent_calls * 10 + 2;
    return 0;
}

static int on_prepare(struct message *msg, char *system_prompt,
                              size_t system_prompt_size, cJSON *messages)
{
    (void)msg;
    (void)messages;
    assert(system_prompt_size > strlen(system_prompt) + 6);
    strcat(system_prompt, " hook");
    prepare_calls++;
    return 0;
}

static int before_run(struct message *msg, const char **model_override,
                              const char *tools_json)
{
    (void)msg;
    assert(strcmp(tools_json, "[]") == 0);
    *model_override = "extension-model";
    seen_model = *model_override;
    before_calls++;
    return 0;
}

static int replace_run_skip(struct message *msg, char *system_prompt,
                                    cJSON *messages, const char *tools_json,
                                    char **out_final_text)
{
    (void)msg;
    (void)system_prompt;
    (void)messages;
    (void)tools_json;
    (void)out_final_text;
    replace_a_calls++;
    return -EIO;
}

static int replace_run_handle(struct message *msg, char *system_prompt,
                                      cJSON *messages, const char *tools_json,
                                      char **out_final_text)
{
    (void)msg;
    (void)system_prompt;
    (void)messages;
    (void)tools_json;
    replace_b_calls++;
    *out_final_text = "handled";
    return 0;
}

static void on_finish(struct message *msg, const char *response)
{
    (void)msg;
    assert(strcmp(response, "done") == 0);
    finish_calls++;
}

static void test_triggers_call_enabled_hooks_in_registration_order(void)
{
    agent_hooks_init();
    struct message msg = {0};
    char prompt[64] = "base";
    const char *model = NULL;

    agent_extension_hooks_t first = {
        .name = "first",
        .on_intent = on_intent_a,
        .on_prepare = on_prepare,
        .before_run = before_run,
        .on_finish = on_finish,
        .enabled = true,
    };
    agent_extension_hooks_t disabled = {
        .name = "disabled",
        .on_intent = on_intent_b,
        .enabled = false,
    };
    agent_extension_hooks_t second = {
        .name = "second",
        .on_intent = on_intent_b,
        .enabled = true,
    };

    agent_hooks_register(&first);
    agent_hooks_register(&disabled);
    agent_hooks_register(&second);

    assert(agent_hooks_trigger_intent(&msg) == 0);
    assert(intent_calls == 12);
    assert(agent_hooks_trigger_prepare(&msg, prompt, sizeof(prompt), NULL) == 0);
    assert(strcmp(prompt, "base hook") == 0);
    assert(prepare_calls == 1);
    assert(agent_hooks_trigger_before_run(&msg, &model, "[]") == 0);
    assert(before_calls == 1);
    assert(model == seen_model);
    assert(strcmp(model, "extension-model") == 0);
    agent_hooks_trigger_finish(&msg, "done");
    assert(finish_calls == 1);
}

static void test_replace_run_uses_first_successful_extension(void)
{
    agent_hooks_init();
    struct message msg = {0};
    char *final_text = NULL;

    agent_extension_hooks_t skip = {
        .name = "skip",
        .replace_run = replace_run_skip,
        .enabled = true,
    };
    agent_extension_hooks_t handle = {
        .name = "handle",
        .replace_run = replace_run_handle,
        .enabled = true,
    };
    agent_extension_hooks_t later = {
        .name = "later",
        .replace_run = replace_run_handle,
        .enabled = true,
    };

    agent_hooks_register(&skip);
    agent_hooks_register(&handle);
    agent_hooks_register(&later);

    assert(agent_hooks_trigger_replace_run(&msg, NULL, NULL, "[]", &final_text) == 0);
    assert(replace_a_calls == 1);
    assert(replace_b_calls == 1);
    assert(strcmp(final_text, "handled") == 0);
}

static void test_replace_run_returns_fail_when_unhandled(void)
{
    agent_hooks_init();
    struct message msg = {0};
    char *final_text = NULL;

    assert(agent_hooks_trigger_replace_run(&msg, NULL, NULL, "[]", &final_text) == ERR_FAIL);
    assert(final_text == NULL);
}

int main(void)
{
    test_triggers_call_enabled_hooks_in_registration_order();
    test_replace_run_uses_first_successful_extension();
    test_replace_run_returns_fail_when_unhandled();
    return 0;
}
