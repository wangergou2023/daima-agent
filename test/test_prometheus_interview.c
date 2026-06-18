#include "interview.h"
#include "drivers/llm/llm_proxy.h"

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int llm_chat_tools(const char *system_prompt,
                           cJSON *messages,
                           const char *tools_json,
                           llm_response_t *resp)
{
    (void)system_prompt;
    (void)messages;
    (void)tools_json;
    assert(resp != NULL);
    memset(resp, 0, sizeof(*resp));
    resp->text = strdup("你想具体实现或修改哪个功能/模块？\n有没有指定文件、技术栈或接口约束？\n完成后你希望用什么结果或测试来验收？");
    resp->text_len = strlen(resp->text);
    return 0;
}

void llm_response_free(llm_response_t *resp)
{
    if (!resp) {
        return;
    }
    free(resp->text);
    memset(resp, 0, sizeof(*resp));
}

static void test_short_vague_request_needs_interview(void)
{
    prometheus_state_t state = {0};

    assert(prometheus_check_needs_interview("加个功能", &state) == 0);
    assert(state.enabled);
    assert(state.needs_interview);
    assert(strstr(state.questions, "具体") != NULL || strstr(state.questions, "哪") != NULL);
    assert(strchr(state.questions, '\n') != NULL);
}

static void test_specific_request_skips_interview(void)
{
    prometheus_state_t state = {0};

    assert(prometheus_check_needs_interview(
               "在 main/agent/agent_loop.c 里为 IMPLEMENT 意图添加访谈逻辑，并新增 2 个单元测试",
               &state) == 0);
    assert(state.enabled);
    assert(!state.needs_interview);
    assert(strcmp(state.questions, "SPECIFIC") == 0 || state.questions[0] == '\0');
}

static void test_invalid_args(void)
{
    prometheus_state_t state = {0};

    assert(prometheus_check_needs_interview(NULL, &state) == ERR_INVALID_ARG);
    assert(prometheus_check_needs_interview("实现功能", NULL) == ERR_INVALID_ARG);
}

int main(void)
{
    test_short_vague_request_needs_interview();
    test_specific_request_skips_interview();
    test_invalid_args();
    printf("test_prometheus_interview: OK\n");
    return 0;
}
