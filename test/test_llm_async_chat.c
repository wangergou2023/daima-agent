#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stddef.h>

#include "drivers/llm/llm_proxy.h"
#include "cjson.h"

void context_fix_truncated_utf8(char *buf, size_t len)
{
    (void)buf;
    (void)len;
}

int main(void)
{
    assert(llm_set_api_key("test-key") == 0);

    cJSON *messages = cJSON_CreateArray();
    assert(messages != NULL);
    cJSON *message = cJSON_CreateObject();
    assert(message != NULL);
    assert(cJSON_AddStringToObject(message, "role", "user") != NULL);
    assert(cJSON_AddStringToObject(message, "content", "ping") != NULL);
    cJSON_AddItemToArray(messages, message);

    llm_async_chat_t *chat = llm_chat_tools_async(
        "You are a terse test assistant.",
        messages,
        NULL,
        NULL);
    assert(chat != NULL);

    (void)llm_chat_async_is_done(chat);
    llm_chat_async_free(chat);
    cJSON_Delete(messages);

    puts("test_llm_async_chat passed");
    return 0;
}
