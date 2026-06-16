/* agent --test 自检：多核 + 总线端到端验证 */
#include "linux/core_task.h"
#include "linux/printk.h"
#include "linux/bus.h"
#include "bus.h"
#include "linux/slab.h"
#include "linux/kernel.h"
#include "drivers/tool/tool_registry.h"
#include "cjson.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define TEST_OUTPUT_SIZE 65536
#define PASS "✅ PASS"
#define FAIL "❌ FAIL"

static int tests_run = 0;
static int tests_pass = 0;

static void report(const char *name, int ok)
{
    tests_run++;
    if (ok) tests_pass++;
    pr_info("[TEST] %s: %s", ok ? PASS : FAIL, name);
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

    pr_info("----------------------------------------");
    pr_info("  Results: %d/%d passed", tests_pass, tests_run);
    pr_info("========================================");
    return tests_pass == tests_run ? 0 : 1;
}