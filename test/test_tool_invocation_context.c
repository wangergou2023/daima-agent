#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "drivers/tool/tool_invocation_context.h"

bool feishu_targets_get_default(char *out, size_t out_size)
{
    if (out && out_size > 0) {
        snprintf(out, out_size, "feishu-default");
    }
    return true;
}

bool feishu_targets_record(const char *route_id,
                           const char *chat_id,
                           const char *chat_type,
                           const char *sender_id)
{
    (void)route_id;
    (void)chat_id;
    (void)chat_type;
    (void)sender_id;
    return true;
}

static llm_tool_call_t call_with(const char *name, const char *input)
{
    llm_tool_call_t call = {0};
    snprintf(call.name, sizeof(call.name), "%s", name);
    call.input = (char *)input;
    call.input_len = strlen(input);
    return call;
}

int main(void)
{
    struct message msg = {0};
    snprintf(msg.channel, sizeof(msg.channel), "%s", CHAN_WEBSOCKET);
    snprintf(msg.chat_id, sizeof(msg.chat_id), "ws-123");

    llm_tool_call_t add = call_with("cron", "{\"action\":\"add\",\"name\":\"n\",\"schedule_type\":\"every\",\"message\":\"m\",\"interval_s\":60}");
    char *patched = tool_invocation_context_patch_input(&add, &msg);
    assert(patched);
    assert(strstr(patched, "\"channel\":\"websocket\""));
    assert(strstr(patched, "\"chat_id\":\"ws-123\""));
    free(patched);

    llm_tool_call_t list = call_with("cron", "{\"action\":\"list\"}");
    assert(tool_invocation_context_patch_input(&list, &msg) == NULL);

    llm_tool_call_t old = call_with("cron_add", "{\"name\":\"n\"}");
    assert(tool_invocation_context_patch_input(&old, &msg) == NULL);

    return 0;
}
