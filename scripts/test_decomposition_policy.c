#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "drivers/tool/tool_decomposition_policy.h"

static void init_message(struct message *msg, const char *content, enum intent intent)
{
    memset(msg, 0, sizeof(*msg));
    strcpy(msg->channel, CHAN_WEBSOCKET);
    strcpy(msg->chat_id, "web_test");
    strcpy(msg->source, MSG_SOURCE_USER);
    msg->content = (char *)content;
    msg->intent = intent;
}

int main(void)
{
    struct message msg = {0};

    init_message(&msg,
                 "分析一下这个代码 /home/wangergou/code/github/oh-my-pi 的代码架构和模块职责",
                 INTENT_INVESTIGATE);
    assert(tool_decomposition_policy_classify_message(&msg) == TOOL_DECOMP_SERIAL);
    assert(tool_decomposition_policy_requires_delegate_only(&msg));

    init_message(&msg,
                 "分别检查 /tmp/a.log 和 /tmp/b.log，找出各自错误并并行处理",
                 INTENT_INVESTIGATE);
    assert(tool_decomposition_policy_classify_message(&msg) == TOOL_DECOMP_PARALLEL);
    assert(tool_decomposition_policy_prefers_parallel(&msg));

    init_message(&msg,
                 "先读 README，再检查 Makefile，最后给我修改建议",
                 INTENT_FIX);
    assert(tool_decomposition_policy_classify_message(&msg) == TOOL_DECOMP_SERIAL);

    init_message(&msg,
                 "读一下 /home/wangergou/code/github/oh-my-pi/README.md",
                 INTENT_QA);
    assert(tool_decomposition_policy_classify_message(&msg) == TOOL_DECOMP_NONE);

    init_message(&msg,
                 "分析这个仓库，但不要并行",
                 INTENT_INVESTIGATE);
    assert(tool_decomposition_policy_classify_message(&msg) == TOOL_DECOMP_NONE);

    init_message(&msg,
                 "1. 先读 README\n2. 再检查 Makefile\n3. 最后给我修改建议",
                 INTENT_FIX);
    assert(tool_decomposition_policy_classify_message(&msg) == TOOL_DECOMP_SERIAL);

    puts("PASS");
    return 0;
}
