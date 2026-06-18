#include <assert.h>
#include <errno.h>
#include <string.h>

#include "intent.h"

int printk(const char *fmt, ...)
{
    (void)fmt;

    return 0;
}

static void assert_intent(const char *message, enum intent expected)
{
    enum intent actual = INTENT_OPEN;
    assert(intent_gate_classify(message, &actual) == 0);
    assert(actual == expected);
}

int main(void)
{
    assert(strcmp(intent_name(INTENT_QA), "QA") == 0);
    assert(strcmp(intent_name(INTENT_IMPLEMENT), "IMPLEMENT") == 0);
    assert(strcmp(intent_name(INTENT_INVESTIGATE), "INVESTIGATE") == 0);
    assert(strcmp(intent_name(INTENT_FIX), "FIX") == 0);
    assert(strcmp(intent_name(INTENT_OPEN), "OPEN") == 0);

    assert_intent("这个功能是什么", INTENT_QA);
    assert_intent("Please ADD a new tool", INTENT_IMPLEMENT);
    assert_intent("帮我分析一下日志", INTENT_INVESTIGATE);
    assert_intent("这个 bug 报错了，帮我 fix", INTENT_FIX);
    assert_intent("今天聊点别的", INTENT_OPEN);
    assert_intent("修复并实现这个功能", INTENT_FIX);

    enum intent intent = INTENT_QA;
    assert(intent_gate_classify(NULL, &intent) == ERR_INVALID_ARG);
    assert(intent_gate_classify("hello", NULL) == ERR_INVALID_ARG);

    return 0;
}
