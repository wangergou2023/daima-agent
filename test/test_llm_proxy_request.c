#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "cjson.h"
#include "drivers/llm/llm_proxy.h"

char s_api_key[320] = {0};
char s_model[64] = "deepseek-v4-pro";
bool s_use_anthropic_api = true;

char *build_request_body(const char *system_prompt,
                         cJSON *messages,
                         const char *tools_json,
                         const char *model_name);

int runtime_config_get_max_output_tokens(void)
{
    return 16384;
}

bool should_disable_thinking(void)
{
    return false;
}

const char *reasoning_effort_for_request(void)
{
    return "high";
}

bool should_add_reasoning_content(void)
{
    return false;
}

bool should_use_max_tokens_field(void)
{
    return true;
}

int printk(const char *fmt, ...)
{
    (void)fmt;
    return 0;
}

void context_fix_truncated_utf8(char *buf, size_t len)
{
    (void)buf;
    (void)len;
}

void log_llm_response_diagnostics(const char *protocol,
                                  const char *raw_resp,
                                  cJSON *root)
{
    (void)protocol;
    (void)raw_resp;
    (void)root;
}

static void test_astral_unicode_keeps_request_json_valid(void)
{
    cJSON *messages = cJSON_CreateArray();
    assert(messages);

    cJSON *user = cJSON_CreateObject();
    assert(user);
    cJSON_AddStringToObject(user, "role", "user");
    cJSON_AddStringToObject(user, "content", "哈喽");
    cJSON_AddItemToArray(messages, user);

    cJSON *assistant = cJSON_CreateObject();
    assert(assistant);
    cJSON_AddStringToObject(assistant, "role", "assistant");
    cJSON_AddStringToObject(assistant, "content", "{\"text\":\"hello 😄\"}");
    cJSON_AddItemToArray(messages, assistant);

    char *body = build_request_body("system", messages, NULL, "deepseek-v4-pro");
    assert(body != NULL);

    cJSON *parsed = cJSON_Parse(body);
    assert(parsed != NULL);

    const char *assistant_content =
        cJSON_GetStringValue(cJSON_GetObjectItem(cJSON_GetArrayItem(
            cJSON_GetObjectItem(parsed, "messages"), 1), "content"));
    assert(assistant_content != NULL);
    assert(strstr(body, "\\uD83D\\uDE04") != NULL || strstr(assistant_content, "😄") != NULL);

    cJSON_Delete(parsed);
    free(body);
    cJSON_Delete(messages);
}

int main(void)
{
    test_astral_unicode_keeps_request_json_valid();
    printf("llm_proxy_request tests passed\n");
    return 0;
}
