#include "drivers/llm/llm_openai_payload.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "cjson.h"

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

static void test_deepseek_thinking_payload(void)
{
    cJSON *messages = cJSON_CreateArray();
    assert(messages);

    cJSON *user = cJSON_CreateObject();
    assert(user);
    cJSON_AddStringToObject(user, "role", "user");
    cJSON_AddStringToObject(user, "content", "hello");
    cJSON_AddItemToArray(messages, user);

    cJSON *body = llm_openai_build_tools_body("system",
                                              messages,
                                              NULL,
                                              "deepseek-reasoner",
                                              4096,
                                              true,
                                              false,
                                              "high",
                                              false);
    assert(body);

    cJSON *thinking = cJSON_GetObjectItemCaseSensitive(body, "thinking");
    assert(thinking && cJSON_IsObject(thinking));
    cJSON *type = cJSON_GetObjectItemCaseSensitive(thinking, "type");
    assert(type && cJSON_IsString(type));
    assert(strcmp(type->valuestring, "enabled") == 0);

    cJSON *effort = cJSON_GetObjectItemCaseSensitive(body, "reasoning_effort");
    assert(effort && cJSON_IsString(effort));
    assert(strcmp(effort->valuestring, "high") == 0);

    cJSON_Delete(body);
    cJSON_Delete(messages);
}

static void test_disabled_thinking_payload(void)
{
    cJSON *body = llm_openai_build_tools_body(NULL,
                                              NULL,
                                              NULL,
                                              "deepseek-chat",
                                              1024,
                                              true,
                                              true,
                                              NULL,
                                              false);
    assert(body);

    cJSON *thinking = cJSON_GetObjectItemCaseSensitive(body, "thinking");
    assert(thinking && cJSON_IsObject(thinking));
    cJSON *type = cJSON_GetObjectItemCaseSensitive(thinking, "type");
    assert(type && cJSON_IsString(type));
    assert(strcmp(type->valuestring, "disabled") == 0);

    cJSON *effort = cJSON_GetObjectItemCaseSensitive(body, "reasoning_effort");
    assert(effort == NULL);

    cJSON_Delete(body);
}

int main(void)
{
    test_deepseek_thinking_payload();
    test_disabled_thinking_payload();
    printf("llm_openai_payload tests passed\n");
    return 0;
}
