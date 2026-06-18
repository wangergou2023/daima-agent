#include "kernel/sched/sched.h"

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "cjson.h"

typedef struct llm_async_chat {
    bool done;
    char text[128];
} test_async_chat_t;

static int g_async_launch_count;

int printk(const char *fmt, ...)
{
    (void)fmt;

    return 0;
}

const char *err_name(int err)
{
    switch (err) {
    case 0: return "OK";
    case -ETIMEDOUT: return "TIMEOUT";
    default: return "ERR";
    }
}

void task_delay(uint32_t delay_ms)
{
    (void)delay_ms;
}

llm_async_chat_t *llm_chat_tools_async(const char *system_prompt,
                                       cJSON *messages,
                                       const char *tools_json,
                                       const char *model_override)
{
    (void)messages;
    (void)tools_json;
    (void)model_override;
    assert(system_prompt != NULL);
    test_async_chat_t *chat = calloc(1, sizeof(*chat));
    assert(chat != NULL);
    chat->done = false;
    snprintf(chat->text, sizeof(chat->text), "async result %d", g_async_launch_count);
    g_async_launch_count++;
    return (llm_async_chat_t *)chat;
}

bool llm_chat_async_is_done(llm_async_chat_t *chat)
{
    test_async_chat_t *test_chat = (test_async_chat_t *)chat;
    return test_chat && test_chat->done;
}

int llm_chat_async_get_response(llm_async_chat_t *chat, llm_response_t *resp)
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

void llm_response_free(llm_response_t *resp)
{
    if (!resp) return;
    free(resp->text);
    memset(resp, 0, sizeof(*resp));
}

static void assert_agent_class(const struct sched_runqueue *rq, int index,
                               enum sched_class_id class)
{
    assert(rq->agents[index].class == class);
    assert(rq->agents[index].prompt_add[0] != '\0');
    assert(rq->agents[index].task_desc[0] != '\0');
    assert(rq->agents[index].state == SCHED_AGENT_WAITING);
    assert(rq->agents[index].error == 0);
}

static void test_implement_dispatches_to_planner_executor_reviewer(void)
{
    struct plan plan = {0};
    struct sched_runqueue rq;
    memset(&rq, 0xA5, sizeof(rq));

    int err = sched_dispatch(INTENT_IMPLEMENT,
                                     &plan,
                                     "create feature",
                                     &rq);

    assert(!err);
    assert(rq.nr_agents == 3);
    assert(!rq.nr_running);
    assert_agent_class(&rq, 0, SCHED_CLASS_PLANNER);
    assert_agent_class(&rq, 1, SCHED_CLASS_EXECUTOR);
    assert_agent_class(&rq, 2, SCHED_CLASS_REVIEWER);
}

static void test_fix_dispatches_to_executor_reviewer(void)
{
    struct plan plan = {0};
    struct sched_runqueue rq = {0};

    int err = sched_dispatch(INTENT_FIX,
                                     &plan,
                                     "fix bug",
                                     &rq);

    assert(!err);
    assert(rq.nr_agents == 2);
    assert_agent_class(&rq, 0, SCHED_CLASS_EXECUTOR);
    assert_agent_class(&rq, 1, SCHED_CLASS_REVIEWER);
}

static void test_qa_dispatches_to_executor(void)
{
    struct sched_runqueue rq = {0};

    int err = sched_dispatch(INTENT_QA,
                                     NULL,
                                     "answer question",
                                     &rq);

    assert(!err);
    assert(rq.nr_agents == 1);
    assert_agent_class(&rq, 0, SCHED_CLASS_EXECUTOR);
}

static void test_merge_results_includes_only_done_agents(void)
{
    struct sched_runqueue rq = {0};
    char output[256];

    rq.nr_agents = 3;
    rq.agents[0].class = SCHED_CLASS_PLANNER;
    rq.agents[0].state = SCHED_AGENT_DONE;
    strcpy(rq.agents[0].result, "plan result");
    rq.agents[1].class = SCHED_CLASS_EXECUTOR;
    rq.agents[1].state = SCHED_AGENT_RUNNING;
    strcpy(rq.agents[1].result, "hidden result");
    rq.agents[2].class = SCHED_CLASS_REVIEWER;
    rq.agents[2].state = SCHED_AGENT_DONE;
    strcpy(rq.agents[2].result, "review result");

    sched_merge(&rq, output, sizeof(output));

    assert(strstr(output, "PLANNER") == NULL);
    assert(strstr(output, "plan result") == NULL);
    assert(strstr(output, "REVIEWER") != NULL);
    assert(strstr(output, "review result") != NULL);
    assert(strstr(output, "hidden result") == NULL);
}

static void test_launch_all_creates_independent_async_requests(void)
{
    struct sched_runqueue rq = {0};
    cJSON *messages = cJSON_CreateArray();
    assert(messages != NULL);
    g_async_launch_count = 0;

    int err = sched_dispatch(INTENT_FIX, NULL, "fix bug", &rq);
    assert(!err);

    sched_start(&rq, "base prompt", messages, "[]");

    assert(g_async_launch_count == 2);
    assert(rq.nr_running == 2);
    for (int i = 0; i < rq.nr_agents; i++) {
        assert(rq.agents[i].async_chat != NULL);
        assert(rq.agents[i].scoped_messages != NULL);
        assert(rq.agents[i].scoped_messages != messages);
        assert(rq.agents[i].state == SCHED_AGENT_RUNNING);
        assert(rq.agents[i].error == 0);
    }

    sched_exit(&rq);
    cJSON_Delete(messages);
}

static void test_wait_all_marks_pending_agents_timed_out(void)
{
    struct sched_runqueue rq = {0};
    cJSON *messages = cJSON_CreateArray();
    assert(messages != NULL);
    g_async_launch_count = 0;

    int err = sched_dispatch(INTENT_FIX, NULL, "fix bug", &rq);
    assert(!err);
    sched_start(&rq, "base prompt", messages, "[]");

    rq.timeout_ms = 0;
    err = sched_wait(&rq);

    assert(!err);
    assert(!rq.nr_running);
    for (int i = 0; i < rq.nr_agents; i++) {
        assert(rq.agents[i].state == SCHED_AGENT_TIMEOUT);
        assert(rq.agents[i].error == ERR_TIMEOUT);
    }

    sched_exit(&rq);
    cJSON_Delete(messages);
}

int main(void)
{
    test_implement_dispatches_to_planner_executor_reviewer();
    test_fix_dispatches_to_executor_reviewer();
    test_qa_dispatches_to_executor();
    test_merge_results_includes_only_done_agents();
    test_launch_all_creates_independent_async_requests();
    test_wait_all_marks_pending_agents_timed_out();
    return 0;
}
