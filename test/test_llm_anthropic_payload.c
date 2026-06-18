#include "drivers/llm/llm_anthropic_payload.h"

#include <assert.h>
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

void llm_response_free(llm_response_t *resp)
{
    if (!resp) {
        return;
    }
    free(resp->text);
    resp->text = NULL;
    resp->text_len = 0;
    free(resp->reasoning_content);
    resp->reasoning_content = NULL;
    resp->reasoning_content_len = 0;
    for (int i = 0; i < resp->call_count; i++) {
        free(resp->calls[i].input);
        resp->calls[i].input = NULL;
        resp->calls[i].input_len = 0;
    }
    resp->call_count = 0;
    resp->tool_use = false;
}

static void test_anthropic_thinking_payload(void)
{
    cJSON *messages = cJSON_CreateArray();
    assert(messages);

    cJSON *user = cJSON_CreateObject();
    assert(user);
    cJSON_AddStringToObject(user, "role", "user");
    cJSON_AddStringToObject(user, "content", "hello");
    cJSON_AddItemToArray(messages, user);

    cJSON *body = llm_anthropic_build_tools_body("system",
                                                 messages,
                                                 NULL,
                                                 "deepseek-v4-pro",
                                                 4096,
                                                 false,
                                                 "high");
    assert(body);

    cJSON *thinking = cJSON_GetObjectItemCaseSensitive(body, "thinking");
    assert(thinking && cJSON_IsObject(thinking));
    cJSON *type = cJSON_GetObjectItemCaseSensitive(thinking, "type");
    assert(type && cJSON_IsString(type));
    assert(strcmp(type->valuestring, "enabled") == 0);

    cJSON *output_config = cJSON_GetObjectItemCaseSensitive(body, "output_config");
    assert(output_config && cJSON_IsObject(output_config));
    cJSON *effort = cJSON_GetObjectItemCaseSensitive(output_config, "effort");
    assert(effort && cJSON_IsString(effort));
    assert(strcmp(effort->valuestring, "high") == 0);

    cJSON_Delete(body);
    cJSON_Delete(messages);
}

static void test_anthropic_parse_thinking_response(void)
{
    const char *json_text =
        "{"
        "\"stop_reason\":\"tool_use\","
        "\"content\":["
          "{\"type\":\"thinking\",\"thinking\":\"step by step\"},"
          "{\"type\":\"tool_use\",\"id\":\"toolu_1\",\"name\":\"weather\",\"input\":{\"city\":\"Nanjing\"}}"
        "]"
        "}";

    llm_response_t resp;
    memset(&resp, 0, sizeof(resp));
    assert(llm_anthropic_parse_response(json_text, &resp) == 0);
    assert(resp.tool_use);
    assert(resp.reasoning_content);
    assert(strcmp(resp.reasoning_content, "step by step") == 0);
    assert(resp.call_count == 1);
    assert(strcmp(resp.calls[0].name, "weather") == 0);
    llm_response_free(&resp);
}

static void test_anthropic_history_with_tool_cycle(void)
{
    cJSON *messages = cJSON_CreateArray();
    assert(messages);

    cJSON *assistant = cJSON_CreateObject();
    cJSON_AddStringToObject(assistant, "role", "assistant");
    cJSON *assistant_content = cJSON_CreateArray();
    cJSON *reasoning = cJSON_CreateObject();
    cJSON_AddStringToObject(reasoning, "type", "reasoning");
    cJSON_AddStringToObject(reasoning, "text", "first think");
    cJSON_AddItemToArray(assistant_content, reasoning);
    cJSON *text = cJSON_CreateObject();
    cJSON_AddStringToObject(text, "type", "text");
    cJSON_AddStringToObject(text, "text", "let me inspect");
    cJSON_AddItemToArray(assistant_content, text);
    cJSON *tool_use = cJSON_CreateObject();
    cJSON_AddStringToObject(tool_use, "type", "tool_use");
    cJSON_AddStringToObject(tool_use, "id", "toolu_1");
    cJSON_AddStringToObject(tool_use, "name", "files");
    cJSON *tool_input = cJSON_CreateObject();
    cJSON_AddStringToObject(tool_input, "action", "read");
    cJSON_AddStringToObject(tool_input, "path", "/tmp/a");
    cJSON_AddItemToObject(tool_use, "input", tool_input);
    cJSON_AddItemToArray(assistant_content, tool_use);
    cJSON_AddItemToObject(assistant, "content", assistant_content);
    cJSON_AddItemToArray(messages, assistant);

    cJSON *user = cJSON_CreateObject();
    cJSON_AddStringToObject(user, "role", "user");
    cJSON *user_content = cJSON_CreateArray();
    cJSON *tool_result = cJSON_CreateObject();
    cJSON_AddStringToObject(tool_result, "type", "tool_result");
    cJSON_AddStringToObject(tool_result, "tool_use_id", "toolu_1");
    cJSON_AddStringToObject(tool_result, "content", "FILE: ok");
    cJSON_AddItemToArray(user_content, tool_result);
    cJSON_AddItemToObject(user, "content", user_content);
    cJSON_AddItemToArray(messages, user);

    cJSON *body = llm_anthropic_build_tools_body("system",
                                                 messages,
                                                 NULL,
                                                 "deepseek-v4-pro",
                                                 4096,
                                                 false,
                                                 "high");
    assert(body);

    cJSON *out_messages = cJSON_GetObjectItemCaseSensitive(body, "messages");
    assert(out_messages && cJSON_IsArray(out_messages));
    cJSON *out_assistant = cJSON_GetArrayItem(out_messages, 0);
    assert(out_assistant);
    cJSON *out_assistant_content = cJSON_GetObjectItemCaseSensitive(out_assistant, "content");
    assert(out_assistant_content && cJSON_IsArray(out_assistant_content));

    char *json = cJSON_PrintUnformatted(body);
    assert(json);
    printf("anthropic history payload: %s\n", json);

    cJSON *out_thinking = cJSON_GetArrayItem(out_assistant_content, 0);
    assert(out_thinking);
    cJSON *out_type = cJSON_GetObjectItemCaseSensitive(out_thinking, "type");
    cJSON *out_text = cJSON_GetObjectItemCaseSensitive(out_thinking, "thinking");
    assert(out_type && cJSON_IsString(out_type) && strcmp(out_type->valuestring, "thinking") == 0);
    assert(out_text && cJSON_IsString(out_text) && strcmp(out_text->valuestring, "first think") == 0);
    free(json);

    cJSON_Delete(body);
    cJSON_Delete(messages);
}

int main(void)
{
    setbuf(stdout, NULL);
    test_anthropic_thinking_payload();
    test_anthropic_parse_thinking_response();
    test_anthropic_history_with_tool_cycle();
    printf("llm_anthropic_payload tests passed\n");
    return 0;
}
