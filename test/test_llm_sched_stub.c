#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "drivers/llm/llm_proxy.h"
#include "cjson.h"

typedef struct llm_async_chat {
    bool done;
    char text[128];
} test_async_chat_t;

static int g_async_launch_count;
static char g_model_name[64] = "stub-model";

llm_async_chat_t *llm_chat_tools_async(const char *system_prompt,
                                       cJSON *messages,
                                       const char *tools_json,
                                       const char *model_override)
{
    (void)messages;
    (void)tools_json;
    if (model_override && model_override[0]) {
        snprintf(g_model_name, sizeof(g_model_name), "%s", model_override);
    }
    assert(system_prompt != NULL);
    test_async_chat_t *chat = calloc(1, sizeof(*chat));
    assert(chat != NULL);
    snprintf(chat->text, sizeof(chat->text), "async result %d", g_async_launch_count++);
    return (llm_async_chat_t *)chat;
}

bool llm_chat_async_is_done(llm_async_chat_t *chat)
{
    test_async_chat_t *test_chat = (test_async_chat_t *)chat;
    return test_chat != NULL;
}

err_t llm_chat_async_get_response(llm_async_chat_t *chat, llm_response_t *resp)
{
    test_async_chat_t *test_chat = (test_async_chat_t *)chat;
    assert(test_chat != NULL);
    assert(resp != NULL);
    resp->text = strdup(test_chat->text);
    assert(resp->text != NULL);
    resp->text_len = strlen(resp->text);
    return 0;
}

void llm_chat_async_free(llm_async_chat_t *chat)
{
    free(chat);
}

err_t llm_chat_tools_with_model(const char *system_prompt,
                                cJSON *messages,
                                const char *tools_json,
                                const char *model_override,
                                llm_response_t *resp)
{
    (void)messages;
    (void)tools_json;
    assert(system_prompt != NULL);
    assert(resp != NULL);
    if (model_override && model_override[0]) {
        snprintf(g_model_name, sizeof(g_model_name), "%s", model_override);
    }
    memset(resp, 0, sizeof(*resp));
    resp->text = strdup("sync stub response");
    assert(resp->text != NULL);
    resp->text_len = strlen(resp->text);
    return 0;
}

void llm_response_free(llm_response_t *resp)
{
    if (!resp) return;
    free(resp->text);
    free(resp->reasoning_content);
    memset(resp, 0, sizeof(*resp));
}

const char *llm_get_model_name(void)
{
    return g_model_name;
}
