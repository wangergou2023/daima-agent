#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "drivers/tool/tool_invocation_context.h"
#include "ipc/bus.h"
#include "kernel/intent.h"

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

int main(void)
{
    struct message msg = {0};
    llm_tool_call_t call = {0};
    char *patched = NULL;

    strscpy(msg.channel, CHAN_WEBSOCKET, sizeof(msg.channel));
    strscpy(msg.chat_id, "web_real_chat", sizeof(msg.chat_id));
    strscpy(msg.source, MSG_SOURCE_USER, sizeof(msg.source));
    msg.intent = INTENT_INVESTIGATE;
    msg.content = strdup("帮我分析 /home/wangergou/code/github/codex 的代码框架和关键模块");
    if (!msg.content) {
        return fail("unable to allocate message content");
    }

    strscpy(call.name, "files", sizeof(call.name));
    call.input = strdup("{\"action\":\"list\",\"path\":\"/home/wangergou/code/github/codex\"}");
    if (!call.input) {
        free(msg.content);
        return fail("unable to allocate tool input");
    }
    call.input_len = strlen(call.input);

    patched = tool_invocation_context_patch_input(&call, &msg);
    if (patched) {
        free(patched);
        free(call.input);
        free(msg.content);
        return fail("files call should not be implicitly rewritten into delegate batch");
    }

    free(call.input);
    free(msg.content);
    puts("PASS");
    return 0;
}
