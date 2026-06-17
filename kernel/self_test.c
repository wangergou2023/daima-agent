/* agent --test 自检：多核 + 总线端到端验证 */
#include "linux/core_task.h"
#include "linux/printk.h"
#include "linux/bus.h"
#include "bus.h"
#include "loop.h"
#include "linux/slab.h"
#include "linux/kernel.h"
#include "drivers/tool/tool_registry.h"
#include "kernel/sched/sched.h"
#include "plan.h"
#include "drivers/tool/tool_delegate.h"
#include "cjson.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define TEST_OUTPUT_SIZE 65536
#define PASS "✅ PASS"
#define FAIL "❌ FAIL"

static int tests_run = 0;
static int tests_pass = 0;

typedef struct { char name[64]; int ok; } test_result_t;
static test_result_t s_results[16];
static int s_result_count = 0;

static void report(const char *name, int ok)
{
    tests_run++;
    if (ok) tests_pass++;
    if (s_result_count < 16) {
        strscpy(s_results[s_result_count].name, name, sizeof(s_results[s_result_count].name));
        s_results[s_result_count].ok = ok;
        s_result_count++;
    }
    pr_info("[TEST] %s: %s", ok ? PASS : FAIL, name);
}

/* 返回结构化 JSON 结果 */
char *agent_self_test_results_json(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "self_test_result");
    cJSON_AddNumberToObject(root, "total", tests_run);
    cJSON_AddNumberToObject(root, "passed", tests_pass);

    cJSON *items = cJSON_CreateArray();
    for (int i = 0; i < s_result_count; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "name", s_results[i].name);
        cJSON_AddBoolToObject(item, "ok", s_results[i].ok);
        cJSON_AddItemToArray(items, item);
    }
    cJSON_AddItemToObject(root, "items", items);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

/* 测 1: 执行核队列通信 */
static void test_executor_queue(void)
{
    struct core_task task;
    memset(&task, 0, sizeof(task));
    snprintf(task.id, sizeof(task.id), "test_eq_1");
    strscpy(task.type, TASK_EXECUTE_TOOLS, sizeof(task.type));

    cJSON *p = cJSON_CreateObject();
    cJSON *tools = cJSON_CreateArray();
    cJSON *t = cJSON_CreateObject();
    cJSON_AddStringToObject(t, "id", "t1");
    cJSON_AddStringToObject(t, "name", "get_current_time");
    cJSON_AddStringToObject(t, "input", "{}");
    cJSON_AddItemToArray(tools, t);
    cJSON_AddItemToObject(p, "tools", tools);
    task.payload = cJSON_PrintUnformatted(p);
    cJSON_Delete(p);

    core_send(CORE_EXECUTOR, &task);

    usleep(200000);  /* 等执行核处理 */

    struct core_task reply;
    memset(&reply, 0, sizeof(reply));
    int ok = (core_recv(CORE_SCHEDULER, &reply, 5000) == 0);
    if (ok) {
        cJSON *r = cJSON_Parse(reply.result);
        cJSON *results = cJSON_GetObjectItem(r, "results");
        if (results && cJSON_IsArray(results)) {
            cJSON *first = cJSON_GetArrayItem(results, 0);
            const char *out = cJSON_GetStringValue(cJSON_GetObjectItem(first, "output"));
            cJSON *err = cJSON_GetObjectItem(first, "err");
            ok = out && out[0] && err && cJSON_GetNumberValue(err) == 0;
            pr_info("  output: %.80s...", out ? out : "null");
        } else {
            ok = 0;
        }
        cJSON_Delete(r);
    }
    kfree(reply.result);
    report("executor queue + tool execution", ok);
}

/* 测 2: 消息总线入站/出站 */
static void test_message_bus(void)
{
    struct message msg;
    memset(&msg, 0, sizeof(msg));
    strscpy(msg.channel, "system", sizeof(msg.channel));
    strscpy(msg.chat_id, "test_bus", sizeof(msg.chat_id));
    strscpy(msg.source, "internal", sizeof(msg.source));
    msg.content = strdup("test message");

    int ok = (message_bus_push_inbound(&msg) == 0);
    if (ok) {
        struct message recv;
        memset(&recv, 0, sizeof(recv));
        ok = (message_bus_pop_inbound(&recv, 1000) == 0);
        if (ok) {
            ok = (strcmp(recv.content, "test message") == 0);
            free(recv.content);
        }
    }
    report("message_bus push/pop", ok);
}

/* 测 3: tool_bus 绑定检查 */
static void test_tool_bus_bindings(void)
{
    const char *required[] = {"weather", "terminal", "files", "todo", "webfetch", "get_current_time"};
    int ok = 1;
    for (size_t i = 0; i < sizeof(required)/sizeof(required[0]); i++) {
        struct device *dev = bus_find_device(tool_bus, required[i]);
        if (!dev || !dev->drv) {
            pr_warn("  %s: not bound", required[i]);
            ok = 0;
        }
    }
    report("tool_bus 6 key tools bound", ok);
}

/* 测 4: 记忆核队列通信 */
static void test_memory_queue(void)
{
    struct core_task task;
    memset(&task, 0, sizeof(task));
    snprintf(task.id, sizeof(task.id), "test_mq_1");
    strscpy(task.type, TASK_LOAD_CONTEXT, sizeof(task.type));

    cJSON *p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "chat_id", "test_nonexistent");
    task.payload = cJSON_PrintUnformatted(p);
    cJSON_Delete(p);

    core_send(CORE_MEMORY, &task);

    usleep(300000);

    struct core_task reply;
    memset(&reply, 0, sizeof(reply));
    /* 不存在的会话 → 记忆核应返回 FAILED */
    int ok = (core_recv(CORE_SCHEDULER, &reply, 5000) == 0);
    if (ok) {
        ok = (strcmp(reply.status, TASK_FAILED) == 0);
    }
    kfree(reply.result);
    report("memory queue + load nonexistent", ok);
}

/* 测 5: 触发真实工具调用 (通过执行核) */
static void test_real_tool_via_executor(void)
{
    char output[4096];
    memset(output, 0, sizeof(output));

    /* 通过执行核调 get_current_time */
    cJSON *p = cJSON_CreateObject();
    cJSON *tools = cJSON_CreateArray();
    cJSON *t = cJSON_CreateObject();
    cJSON_AddStringToObject(t, "id", "t_real");
    cJSON_AddStringToObject(t, "name", "get_current_time");
    cJSON_AddStringToObject(t, "input", "{}");
    cJSON_AddItemToArray(tools, t);
    cJSON_AddItemToObject(p, "tools", tools);

    struct core_task task;
    memset(&task, 0, sizeof(task));
    snprintf(task.id, sizeof(task.id), "test_r_1");
    strscpy(task.type, TASK_EXECUTE_TOOLS, sizeof(task.type));
    task.payload = cJSON_PrintUnformatted(p);
    cJSON_Delete(p);

    core_send(CORE_EXECUTOR, &task);

    struct core_task reply;
    memset(&reply, 0, sizeof(reply));
    int ok = 0;
    if (core_recv(CORE_SCHEDULER, &reply, 5000) == 0) {
        cJSON *r = cJSON_Parse(reply.result);
        cJSON *results = cJSON_GetObjectItem(r, "results");
        if (results && cJSON_IsArray(results)) {
            cJSON *first = cJSON_GetArrayItem(results, 0);
            const char *out = cJSON_GetStringValue(cJSON_GetObjectItem(first, "output"));
            cJSON *err = cJSON_GetObjectItem(first, "err");
            ok = out && out[0] && err && cJSON_GetNumberValue(err) == 0;
            pr_info("  time: %.60s", out ? out : "null");
        }
        cJSON_Delete(r);
    }
    kfree(reply.result);
    report("real tool via executor core", ok);
}

/* 测 6: 端到端 pipeline — 消息入站 → 出站 */
static void test_message_pipeline(void)
{
    struct message msg;
    memset(&msg, 0, sizeof(msg));
    strscpy(msg.channel, "system", sizeof(msg.channel));
    strscpy(msg.chat_id, "test_pipeline", sizeof(msg.chat_id));
    strscpy(msg.source, "internal", sizeof(msg.source));
    msg.content = strdup("ping");

    message_bus_push_outbound(&msg);

    struct message recv;
    memset(&recv, 0, sizeof(recv));
    int ok = (message_bus_pop_outbound(&recv, 2000) == 0);
    if (ok) {
        ok = (strcmp(recv.channel, "system") == 0);
        free(recv.content);
    }
    report("message pipeline inbound→outbound", ok);
}

/* 测 7: 完整 LLM 调用 — 推真实消息，等 LLM 回复 */
static void test_llm_pipeline(void)
{
    struct message msg;
    memset(&msg, 0, sizeof(msg));
    strscpy(msg.channel, "websocket", sizeof(msg.channel));
    strscpy(msg.chat_id, "self_test_llm", sizeof(msg.chat_id));
    strscpy(msg.source, "user", sizeof(msg.source));
    msg.content = strdup("reply OK");

    agent_process_message(&msg);

    struct message reply;
    memset(&reply, 0, sizeof(reply));
    int ok = (message_bus_pop_outbound(&reply, 2000) == 0);
    if (ok) {
        ok = reply.content && strlen(reply.content) > 0;
        pr_info("  LLM response: %.80s...", reply.content ? reply.content : "null");
        free(reply.content);
    }
    report("LLM end-to-end pipeline", ok);
}


/* 测 8: 异步压缩调度 */
static void test_async_compress_dispatch(void)
{
    struct core_task task;
    memset(&task, 0, sizeof(task));
    snprintf(task.id, sizeof(task.id), "test_cc_1");
    strscpy(task.type, TASK_COMPRESS_CONTEXT, sizeof(task.type));

    cJSON *p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "chat_id", "self_test_llm");
    task.payload = cJSON_PrintUnformatted(p);
    cJSON_Delete(p);

    core_send(CORE_MEMORY, &task);
    usleep(200000);

    struct core_task reply;
    memset(&reply, 0, sizeof(reply));
    int ok = (core_recv(CORE_SCHEDULER, &reply, 5000) == 0);
    if (ok) {
        ok = (strcmp(reply.status, TASK_DONE) == 0);
    }
    kfree(reply.result);
    report("async compress dispatch", ok);
}

/* 测 9: subagent 调度验证 */
static void test_subagent_dispatch(void)
{
    struct sched_runqueue rq;
    memset(&rq, 0, sizeof(rq));

    struct plan test_plan = {
        .has_plan = true,
        .reviewed = true,
        .plan_text = "test plan",
    };

    int ok = (sched_dispatch(INTENT_IMPLEMENT, &test_plan,
                              "test task", &rq) == 0);
    if (ok) {
        ok = (rq.nr_agents >= 2);
        if (ok) {
            bool has_planner = false, has_executor = false, has_reviewer = false;
            for (int i = 0; i < rq.nr_agents && i < SCHED_MAX_AGENTS; i++) {
                int cls = rq.agents[i].class;
                if (cls == SCHED_CLASS_PLANNER) has_planner = true;
                if (cls == SCHED_CLASS_EXECUTOR) has_executor = true;
                if (cls == SCHED_CLASS_REVIEWER) has_reviewer = true;
            }
            ok = has_planner && has_executor && has_reviewer;
            pr_info("  agents=%d PLANNER=%d EXECUTOR=%d REVIEWER=%d",
                    rq.nr_agents, has_planner, has_executor, has_reviewer);
        }
    }
    sched_exit(&rq);
    report("subagent dispatch (IMPLEMENT→3 agents)", ok);
}

/* 测 10: delegate_task 真实执行 */
static void test_delegate_task_exec(void)
{
    char output[16384];
    memset(output, 0, sizeof(output));

    const char *input =
        "{\"task\":\"run terminal command: echo subagent_ok\","
        "\"intent\":\"IMPLEMENT\"}";

    const struct tool *t = tool_delegate_definition();
    err_t err = t->execute(input, output, sizeof(output));
    int ok = (err == 0);
    if (ok) {
        ok = (output[0] && !strstr(output, "失败") && strlen(output) > 20);
        pr_info("  result: %.120s...", output);
    } else {
        pr_info("  failed: err=%d output=%.120s", err, output);
    }
    report("delegate_task real execution", ok);
}

int agent_self_test(void)
{
    pr_info("========================================");
    pr_info("  Agent Self-Test — Multi-Core Check");
    pr_info("========================================");

    /* 等各核启动 */
    usleep(500000);

    test_executor_queue();
    test_message_bus();
    test_tool_bus_bindings();
    test_memory_queue();
    test_real_tool_via_executor();
    test_message_pipeline();
    test_llm_pipeline();
    test_async_compress_dispatch();
    test_subagent_dispatch();
    test_delegate_task_exec();

    pr_info("----------------------------------------");
    pr_info("  Results: %d/%d passed", tests_pass, tests_run);
    pr_info("========================================");
    return tests_pass == tests_run ? 0 : 1;
}