#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "cjson.h"
#include "drivers/llm/llm_anthropic_payload.h"

void context_fix_truncated_utf8(char *buf, size_t len)
{
    (void)buf;
    (void)len;
}

int printk(const char *fmt, ...)
{
    (void)fmt;
    return 0;
}

static int fail(const char *message)
{
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

static cJSON *make_message_string(const char *role, const char *content)
{
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "role", role);
    cJSON_AddStringToObject(msg, "content", content);
    return msg;
}

static cJSON *make_message_array(const char *role)
{
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "role", role);
    cJSON_AddItemToObject(msg, "content", cJSON_CreateArray());
    return msg;
}

static cJSON *make_tool_result_block(const char *tool_use_id, const char *content)
{
    cJSON *block = cJSON_CreateObject();
    cJSON_AddStringToObject(block, "type", "tool_result");
    if (tool_use_id) {
        cJSON_AddStringToObject(block, "tool_use_id", tool_use_id);
    }
    if (content) {
        cJSON_AddStringToObject(block, "content", content);
    }
    return block;
}

int main(void)
{
    int rc = 1;
    cJSON *messages = cJSON_CreateArray();
    cJSON *body = NULL;
    cJSON *out_messages;
    cJSON *m0;
    cJSON *m1;
    cJSON *m2;
    cJSON *content0;
    cJSON *content1;
    cJSON *text_block;
    cJSON *tool_block;
    cJSON *empty_assistant;
    cJSON *invalid_tool_user;

    if (!messages) {
        return fail("unable to create messages");
    }

    cJSON_AddItemToArray(messages, make_message_string("user", "hello"));

    invalid_tool_user = make_message_array("user");
    if (!invalid_tool_user) {
        goto cleanup;
    }
    cJSON_AddItemToArray(cJSON_GetObjectItem(invalid_tool_user, "content"),
                         make_tool_result_block(NULL, "missing id"));
    cJSON_AddItemToArray(cJSON_GetObjectItem(invalid_tool_user, "content"),
                         make_tool_result_block("toolu_1", "ok"));
    cJSON_AddItemToArray(messages, invalid_tool_user);

    empty_assistant = make_message_array("assistant");
    if (!empty_assistant) {
        goto cleanup;
    }
    cJSON_AddItemToArray(messages, empty_assistant);

    body = llm_anthropic_build_tools_body("sys", messages, NULL, "claude-test", 256, true, NULL, false);
    if (!body) {
        goto cleanup;
    }

    out_messages = cJSON_GetObjectItem(body, "messages");
    if (!out_messages || !cJSON_IsArray(out_messages)) {
        return fail("messages missing from anthropic body");
    }
    if (cJSON_GetArraySize(out_messages) != 2) {
        return fail("expected invalid tool_result and empty assistant to be filtered");
    }

    m0 = cJSON_GetArrayItem(out_messages, 0);
    content0 = cJSON_GetObjectItem(m0, "content");
    if (!content0 || !cJSON_IsArray(content0) || cJSON_GetArraySize(content0) != 1) {
        return fail("string content was not converted into block array");
    }
    text_block = cJSON_GetArrayItem(content0, 0);
    if (strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(text_block, "type")), "text") != 0) {
        return fail("first block type is not text");
    }
    if (strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(text_block, "text")), "hello") != 0) {
        return fail("text block content mismatch");
    }

    m1 = cJSON_GetArrayItem(out_messages, 1);
    content1 = cJSON_GetObjectItem(m1, "content");
    if (!content1 || !cJSON_IsArray(content1) || cJSON_GetArraySize(content1) != 1) {
        return fail("tool_result filtering produced unexpected block count");
    }
    tool_block = cJSON_GetArrayItem(content1, 0);
    if (strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(tool_block, "tool_use_id")), "toolu_1") != 0) {
        return fail("valid tool_result tool_use_id missing");
    }

    m2 = cJSON_GetArrayItem(out_messages, 2);
    if (m2) {
        return fail("empty assistant should not survive conversion");
    }

    rc = 0;
cleanup:
    cJSON_Delete(body);
    cJSON_Delete(messages);
    if (rc == 0) {
        puts("PASS");
        return 0;
    }
    return fail("anthropic payload regression");
}
