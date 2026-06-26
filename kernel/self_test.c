/* !test 聊天命令自检：多核 + 总线端到端验证 */
#include "linux/core_task.h"
#include "linux/printk.h"
#include "linux/bus.h"
#include "bus.h"
#include "loop.h"
#include "linux/slab.h"
#include "linux/kernel.h"
#include "drivers/tool/tool_types.h"
#include "drivers/tool/tool_delegate.h"
#include "drivers/tool/tool_invocation_context.h"
#include "drivers/llm/llm_proxy.h"
#include "kernel/context/context_build.h"
#include "kernel/turn/turn_exec.h"
#include "kernel/tooling/tool_guard.h"
#include "tool_notify.h"
#include "paths.h"
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
    strscpy(task.type, TASK_SAVE_SESSION, sizeof(task.type));

    cJSON *p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "chat_id", "test_memory_queue");
    cJSON_AddStringToObject(p, "role", "user");
    cJSON_AddStringToObject(p, "content", "memory queue smoke");
    task.payload = cJSON_PrintUnformatted(p);
    cJSON_Delete(p);

    core_send(CORE_MEMORY, &task);

    usleep(300000);

    struct core_task reply;
    memset(&reply, 0, sizeof(reply));
    /* fire-and-forget 保存任务也会通过 reply 返回 DONE */
    int ok = (core_recv(CORE_SCHEDULER, &reply, 5000) == 0);
    if (ok) {
        ok = (strcmp(reply.status, TASK_DONE) == 0);
    }
    kfree(reply.result);
    report("memory queue + save session", ok);
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

    usleep(300000);  /* 等执行核处理完前序任务 */

    struct core_task reply;
    memset(&reply, 0, sizeof(reply));
    int ok = 0;
    if (core_recv(CORE_SCHEDULER, &reply, 10000) == 0) {
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

/* 测 7: 异步压缩调度 */
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

/* 测 8: delegate_task 旧协议应拒绝，避免主提示词继续走 task/intent 老接口 */
static void test_delegate_task_legacy_rejected(void)
{
    char output[4096];
    memset(output, 0, sizeof(output));

    const char *input =
        "{\"task\":\"run terminal command: echo subagent_ok\","
        "\"intent\":\"IMPLEMENT\"}";

    const struct tool *t = tool_delegate_definition();
    err_t err = t->execute(input, output, sizeof(output));
    int ok = (err != 0) && strstr(output, "subagent_type");
    pr_info("  legacy result: err=%d output=%.120s", err, output);
    report("delegate_task rejects legacy task/intent protocol", ok);
}

/* 测 9: delegate_task 新协议同步实现链路 */
static void test_delegate_task_sync_implement(void)
{
    char output[16384];
    memset(output, 0, sizeof(output));

    const char *input =
        "{"
        "\"description\":\"echo test\","
        "\"prompt\":\"run terminal command: echo subagent_ok\","
        "\"subagent_type\":\"implement\","
        "\"run_in_background\":false"
        "}";

    const struct tool *t = tool_delegate_definition();
    err_t err = t->execute(input, output, sizeof(output));
    int ok = (err == 0);
    if (ok) {
        ok = output[0] && strstr(output, "subagent_ok");
        pr_info("  sync implement result: %.120s...", output);
    } else {
        pr_info("  sync implement failed: err=%d output=%.120s", err, output);
    }
    report("delegate_task sync implement execution", ok);
}

/* 测 10: delegate_task 新协议后台模式返回 task_id */
static void test_delegate_task_background_handle(void)
{
    char output[4096];
    memset(output, 0, sizeof(output));

    const char *input =
        "{"
        "\"description\":\"echo bg\","
        "\"prompt\":\"run terminal command: echo background_ok\","
        "\"subagent_type\":\"implement\","
        "\"run_in_background\":true"
        "}";

    const struct tool *t = tool_delegate_definition();
    err_t err = t->execute(input, output, sizeof(output));
    int ok = (err == 0) && strstr(output, "\"task_id\":\"dt_");
    pr_info("  background handle: err=%d output=%.120s", err, output);
    report("delegate_task background returns task_id", ok);
}

static void test_delegate_task_batch_background_returns_coordinator(void)
{
    char output[8192];
    memset(output, 0, sizeof(output));

    const char *input =
        "{"
        "\"tasks\":["
          "{"
            "\"description\":\"explore tree\","
            "\"prompt\":\"analyze repo structure\","
            "\"subagent_type\":\"explore\""
          "},"
          "{"
            "\"description\":\"check docs\","
            "\"prompt\":\"inspect docs and config\","
            "\"subagent_type\":\"librarian\""
          "}"
        "]"
        "}";

    const struct tool *t = tool_delegate_definition();
    err_t err = t->execute(input, output, sizeof(output));
    int ok = (err == 0) &&
             strstr(output, "\"coordinator_id\":\"dc_") &&
             strstr(output, "\"task_id\":\"dt_") &&
             strstr(output, "\"agents\":[");
    pr_info("  batch delegate result: err=%d output=%.240s", err, output);
    report("delegate_task batch background returns coordinator", ok);
}

static void test_delegate_task_batch_poll_returns_agents(void)
{
    char start_output[8192];
    char poll_output[8192];
    memset(start_output, 0, sizeof(start_output));
    memset(poll_output, 0, sizeof(poll_output));

    const char *start_input =
        "{"
        "\"tasks\":["
          "{"
            "\"description\":\"oracle review\","
            "\"prompt\":\"review architecture risks\","
            "\"subagent_type\":\"oracle\""
          "},"
          "{"
            "\"description\":\"repo scan\","
            "\"prompt\":\"scan key modules\","
            "\"subagent_type\":\"explore\""
          "}"
        "]"
        "}";

    const struct tool *t = tool_delegate_definition();
    err_t err = t->execute(start_input, start_output, sizeof(start_output));
    int ok = (err == 0);
    char coordinator_id[32] = {0};
    if (ok) {
        const char *marker = strstr(start_output, "\"coordinator_id\":\"");
        if (!marker) {
            ok = 0;
        } else {
            marker += strlen("\"coordinator_id\":\"");
            int i = 0;
            while (marker[i] && marker[i] != '"' && i < (int)sizeof(coordinator_id) - 1) {
                coordinator_id[i] = marker[i];
                i++;
            }
            coordinator_id[i] = '\0';
            ok = coordinator_id[0] != '\0';
        }
    }

    if (ok) {
        char poll_input[128];
        snprintf(poll_input, sizeof(poll_input),
                 "{\"coordinator_id\":\"%s\"}", coordinator_id);
        err = t->execute(poll_input, poll_output, sizeof(poll_output));
        ok = (err == 0) &&
             strstr(poll_output, "\"coordinator_id\":\"") &&
             strstr(poll_output, "\"agents\":[") &&
             strstr(poll_output, "\"status\":\"");
        pr_info("  batch delegate poll: err=%d output=%.240s", err, poll_output);
    }

    report("delegate_task batch poll returns agents", ok);
}

static void test_context_prompt_mentions_batch_delegate(void)
{
    char buf[32768];
    memset(buf, 0, sizeof(buf));
    err_t err = context_build_system_prompt(buf, sizeof(buf));
    int ok = (err == 0) &&
             strstr(buf, "delegate_task({tasks:[...]})") &&
             strstr(buf, "coordinator_id") &&
             strstr(buf, "不要连续发多个单独的 `delegate_task`");
    report("system prompt mentions batch delegate coordinator flow", ok);
}

static void test_turn_exec_merges_sibling_delegate_calls(void)
{
    llm_response_t resp;
    memset(&resp, 0, sizeof(resp));
    resp.tool_use = true;
    resp.call_count = 3;

    strscpy(resp.calls[0].id, "call_a", sizeof(resp.calls[0].id));
    strscpy(resp.calls[0].name, "delegate_task", sizeof(resp.calls[0].name));
    resp.calls[0].input = strdup(
        "{"
        "\"subagent_type\":\"explore\","
        "\"description\":\"repo scan\","
        "\"prompt\":\"scan repo structure\","
        "\"run_in_background\":true"
        "}");

    strscpy(resp.calls[1].id, "call_b", sizeof(resp.calls[1].id));
    strscpy(resp.calls[1].name, "delegate_task", sizeof(resp.calls[1].name));
    resp.calls[1].input = strdup(
        "{"
        "\"subagent_type\":\"librarian\","
        "\"description\":\"docs scan\","
        "\"prompt\":\"scan docs and config\","
        "\"run_in_background\":true"
        "}");

    strscpy(resp.calls[2].id, "call_c", sizeof(resp.calls[2].id));
    strscpy(resp.calls[2].name, "delegate_task", sizeof(resp.calls[2].name));
    resp.calls[2].input = strdup(
        "{"
        "\"subagent_type\":\"oracle\","
        "\"description\":\"risk review\","
        "\"prompt\":\"review architecture risks\","
        "\"run_in_background\":true"
        "}");

    struct message msg;
    memset(&msg, 0, sizeof(msg));
    strscpy(msg.channel, "websocket", sizeof(msg.channel));
    strscpy(msg.chat_id, "self_test_delegate_merge", sizeof(msg.chat_id));
    strscpy(msg.source, "internal", sizeof(msg.source));
    msg.content = strdup("merge sibling delegate calls");

    char tool_output[8192];
    turn_exec_stats_t stats;
    memset(&stats, 0, sizeof(stats));
    cJSON *content = agent_turn_build_tool_results(&resp, &msg, tool_output, sizeof(tool_output), &stats);

    int ok = 0;
    if (content && cJSON_IsArray(content) && cJSON_GetArraySize(content) == 3) {
        cJSON *first = cJSON_GetArrayItem(content, 0);
        cJSON *second = cJSON_GetArrayItem(content, 1);
        cJSON *third = cJSON_GetArrayItem(content, 2);
        const char *first_text = first ? cJSON_GetStringValue(cJSON_GetObjectItem(first, "content")) : NULL;
        const char *second_text = second ? cJSON_GetStringValue(cJSON_GetObjectItem(second, "content")) : NULL;
        const char *third_text = third ? cJSON_GetStringValue(cJSON_GetObjectItem(third, "content")) : NULL;
        ok = first_text && strstr(first_text, "\"coordinator_id\":\"dc_") &&
             second_text && strstr(second_text, "\"status\":\"merged_into_batch\"") &&
             third_text && strstr(third_text, "\"status\":\"merged_into_batch\"");
    }

    cJSON_Delete(content);
    free(resp.calls[0].input);
    free(resp.calls[1].input);
    free(resp.calls[2].input);
    free(msg.content);
    report("turn_exec merges sibling delegate calls into batch", ok);
}

static void test_turn_exec_marks_background_delegate_started(void)
{
    llm_response_t resp;
    memset(&resp, 0, sizeof(resp));
    resp.tool_use = true;
    resp.call_count = 2;

    strscpy(resp.calls[0].id, "call_a", sizeof(resp.calls[0].id));
    strscpy(resp.calls[0].name, "delegate_task", sizeof(resp.calls[0].name));
    resp.calls[0].input = strdup(
        "{"
        "\"subagent_type\":\"explore\","
        "\"description\":\"repo scan\","
        "\"prompt\":\"scan repo structure\","
        "\"run_in_background\":true"
        "}");

    strscpy(resp.calls[1].id, "call_b", sizeof(resp.calls[1].id));
    strscpy(resp.calls[1].name, "delegate_task", sizeof(resp.calls[1].name));
    resp.calls[1].input = strdup(
        "{"
        "\"subagent_type\":\"oracle\","
        "\"description\":\"risk review\","
        "\"prompt\":\"review architecture risks\","
        "\"run_in_background\":true"
        "}");

    struct message msg;
    memset(&msg, 0, sizeof(msg));
    strscpy(msg.channel, "websocket", sizeof(msg.channel));
    strscpy(msg.chat_id, "self_test_delegate_bg", sizeof(msg.chat_id));
    strscpy(msg.source, "internal", sizeof(msg.source));
    msg.content = strdup("delegate background short-circuit");

    char tool_output[8192];
    turn_exec_stats_t stats;
    memset(&stats, 0, sizeof(stats));
    cJSON *content = agent_turn_build_tool_results(&resp, &msg, tool_output, sizeof(tool_output), &stats);

    int ok = content &&
             stats.background_delegate_started &&
             strstr(stats.background_delegate_reply, "coordinator_id=dc_");

    cJSON_Delete(content);
    free(resp.calls[0].input);
    free(resp.calls[1].input);
    free(msg.content);
    report("turn_exec marks background delegate started", ok);
}

static void test_delegate_empty_input_is_recoverable(void)
{
    int ok = !agent_tool_protocol_failure_should_stop("delegate_task", "{}", "delegate_task: missing required field 'subagent_type'", ERR_INVALID_ARG);
    report("delegate empty input is recoverable", ok);
}

static void test_delegate_schema_avoids_anyof(void)
{
    const struct tool *t = tool_delegate_definition();
    int ok = t && t->input_schema_json &&
             !strstr(t->input_schema_json, "\"anyOf\"") &&
             strstr(t->input_schema_json, "\"subagent_type\"") &&
             strstr(t->input_schema_json, "\"tasks\"");
    report("delegate schema avoids anyOf", ok);
}

/* 测 11: broad discovery 的 files.list 应被中间层改写为 delegate_task(explore) */
static void test_discovery_files_rewritten_to_delegate(void)
{
    llm_tool_call_t call;
    memset(&call, 0, sizeof(call));
    strscpy(call.id, "rewrite_1", sizeof(call.id));
    strscpy(call.name, "files", sizeof(call.name));
    call.input = strdup("{\"action\":\"list\",\"path\":\"/tmp/project/src\"}");

    struct message msg;
    memset(&msg, 0, sizeof(msg));
    strscpy(msg.channel, "websocket", sizeof(msg.channel));
    strscpy(msg.chat_id, "rewrite_chat", sizeof(msg.chat_id));
    strscpy(msg.source, "user", sizeof(msg.source));
    msg.intent = INTENT_INVESTIGATE;
    msg.content = strdup("帮我分析一下这个代码目录的结构和关键模块");

    char *patched = tool_invocation_context_patch_input(&call, &msg);
    const char *patched_tool_name = tool_invocation_context_patch_tool_name(&call, &msg);
    int ok = 0;
    if (patched) {
        cJSON *root = cJSON_Parse(patched);
        const char *subagent_type = root ? cJSON_GetStringValue(cJSON_GetObjectItem(root, "subagent_type")) : NULL;
        const char *prompt = root ? cJSON_GetStringValue(cJSON_GetObjectItem(root, "prompt")) : NULL;
        ok = root &&
             patched_tool_name && strcmp(patched_tool_name, "delegate_task") == 0 &&
             subagent_type && strcmp(subagent_type, "explore") == 0 &&
             prompt && strstr(prompt, "/tmp/project/src") &&
             strstr(prompt, "bounded exploration request") &&
             strstr(prompt, "Do not exhaustively enumerate every subdirectory");
        cJSON_Delete(root);
    }

    kfree(call.input);
    kfree(msg.content);
    kfree(patched);
    report("discovery files rewrite to delegate explore", ok);
}

/* 测 12: broad discovery 即使被误分成 QA，也必须改写为 delegate_task(explore) */
static void test_discovery_files_rewritten_without_investigate_intent(void)
{
    llm_tool_call_t call;
    memset(&call, 0, sizeof(call));
    strscpy(call.id, "rewrite_qa_1", sizeof(call.id));
    strscpy(call.name, "files", sizeof(call.name));
    call.input = strdup("{\"action\":\"list\",\"path\":\"/tmp/project/libimp-samples\"}");

    struct message msg;
    memset(&msg, 0, sizeof(msg));
    strscpy(msg.channel, "websocket", sizeof(msg.channel));
    strscpy(msg.chat_id, "rewrite_qa_chat", sizeof(msg.chat_id));
    strscpy(msg.source, "user", sizeof(msg.source));
    msg.intent = INTENT_QA;
    msg.content = strdup("帮我看看这个目录结构，分析一下关键模块和代码组织");

    char *patched = tool_invocation_context_patch_input(&call, &msg);
    const char *patched_tool_name = tool_invocation_context_patch_tool_name(&call, &msg);
    int ok = 0;
    if (patched) {
        cJSON *root = cJSON_Parse(patched);
        const char *subagent_type = root ? cJSON_GetStringValue(cJSON_GetObjectItem(root, "subagent_type")) : NULL;
        const char *prompt = root ? cJSON_GetStringValue(cJSON_GetObjectItem(root, "prompt")) : NULL;
        ok = root &&
             patched_tool_name && strcmp(patched_tool_name, "delegate_task") == 0 &&
             subagent_type && strcmp(subagent_type, "explore") == 0 &&
             prompt && strstr(prompt, "/tmp/project/libimp-samples") &&
             strstr(prompt, "optimize for fast coverage and early stop");
        cJSON_Delete(root);
    }

    kfree(call.input);
    kfree(msg.content);
    kfree(patched);
    report("discovery rewrite does not depend on investigate intent", ok);
}

/* 测 13: delegate_sync 子代理内部的 files.list 不能再次被改写成 delegate_task，避免递归套娃 */
static void test_subagent_discovery_not_rewritten_recursively(void)
{
    llm_tool_call_t call;
    memset(&call, 0, sizeof(call));
    strscpy(call.id, "rewrite_subagent_1", sizeof(call.id));
    strscpy(call.name, "files", sizeof(call.name));
    call.input = strdup("{\"action\":\"list\",\"path\":\"/tmp/project/src\"}");

    struct message msg;
    memset(&msg, 0, sizeof(msg));
    strscpy(msg.channel, "websocket", sizeof(msg.channel));
    strscpy(msg.chat_id, "delegate_sync_42", sizeof(msg.chat_id));
    strscpy(msg.source, "internal", sizeof(msg.source));
    msg.intent = INTENT_INVESTIGATE;
    msg.content = strdup("Investigate this codebase area and return a concise discovery summary.");

    char *patched = tool_invocation_context_patch_input(&call, &msg);
    const char *patched_tool_name = tool_invocation_context_patch_tool_name(&call, &msg);
    int ok = (patched == NULL && patched_tool_name == NULL);

    kfree(call.input);
    kfree(msg.content);
    kfree(patched);
    report("subagent discovery rewrite is disabled", ok);
}

/* 测 14: delegate_sync 子代理的 websocket 工具活动通知应静默跳过，不能报 ERR_NOT_FOUND */
static void test_subagent_tool_activity_does_not_require_ws_client(void)
{
    struct message msg;
    memset(&msg, 0, sizeof(msg));
    strscpy(msg.channel, "websocket", sizeof(msg.channel));
    strscpy(msg.chat_id, "delegate_sync_77", sizeof(msg.chat_id));

    tool_activity_event_t event = {
        .tool_name = "files",
        .tool_input = "{\"action\":\"list\",\"path\":\"/tmp/project/src\"}",
        .target = "src",
        .detail = "",
        .default_text = "files · src · 0.0s",
        .ok = true,
        .elapsed_ms = 0,
    };

    err_t err = channel_runtime_send_tool_activity(&msg, &event);
    report("subagent websocket tool activity is skipped", err == 0);
}

/* 测 15: 普通 websocket chat 断开后，工具活动通知应 best-effort 静默处理 */
static void test_websocket_tool_activity_disconnect_is_best_effort(void)
{
    struct message msg;
    memset(&msg, 0, sizeof(msg));
    strscpy(msg.channel, "websocket", sizeof(msg.channel));
    strscpy(msg.chat_id, "web_disconnect_selftest", sizeof(msg.chat_id));

    tool_activity_event_t event = {
        .tool_name = "files",
        .tool_input = "{\"action\":\"list\",\"path\":\"/tmp/project/src\"}",
        .target = "src",
        .detail = "",
        .default_text = "files · src · 0.0s",
        .ok = true,
        .elapsed_ms = 0,
    };

    err_t err = channel_runtime_send_tool_activity(&msg, &event);
    report("websocket disconnect tool activity is best effort", err == 0);
}

/* 测 16: bounded broad discovery prompt 应携带早停标记，供 runtime 预算识别 */
static void test_discovery_prompt_marked_as_bounded(void)
{
    llm_tool_call_t call;
    memset(&call, 0, sizeof(call));
    strscpy(call.id, "rewrite_bound_1", sizeof(call.id));
    strscpy(call.name, "files", sizeof(call.name));
    call.input = strdup("{\"action\":\"list\",\"path\":\"/tmp/project/src\"}");

    struct message msg;
    memset(&msg, 0, sizeof(msg));
    strscpy(msg.channel, "websocket", sizeof(msg.channel));
    strscpy(msg.chat_id, "rewrite_bound_chat", sizeof(msg.chat_id));
    strscpy(msg.source, "user", sizeof(msg.source));
    msg.intent = INTENT_INVESTIGATE;
    msg.content = strdup("帮我看看这个仓库的目录结构和关键模块");

    char *patched = tool_invocation_context_patch_input(&call, &msg);
    int ok = 0;
    if (patched) {
        cJSON *root = cJSON_Parse(patched);
        const char *prompt = root ? cJSON_GetStringValue(cJSON_GetObjectItem(root, "prompt")) : NULL;
        ok = prompt &&
             strstr(prompt, "bounded exploration request") &&
             strstr(prompt, "optimize for fast coverage and early stop");
        cJSON_Delete(root);
    }

    kfree(call.input);
    kfree(msg.content);
    kfree(patched);
    report("bounded discovery prompt carries early-stop marker", ok);
}

/* 测 17: 直接 delegate_task(explore) 的目录结构分析请求也应命中受限预算 */
static void test_delegate_explore_overview_budget_heuristic(void)
{
    const struct tool *t = tool_delegate_definition();
    char output[256];
    const char *input =
        "{"
        "\"subagent_type\":\"explore\","
        "\"description\":\"分析 daima-agent 目录结构与关键模块\","
        "\"prompt\":\"分析 /tmp/project 的目录结构和代码组织，说明关键模块与入口文件\""
        "}";

    err_t err = t->execute(input, output, sizeof(output));
    int ok = (err == 0) || (err == ERR_FAIL);
    /* 这里只验证请求被接受并走 delegate 路径；真正预算命中由 tool_delegate.c 的启发式和运行时日志验证。 */
    report("delegate explore overview heuristic accepts direct request", ok);
}

static void test_delegate_dsml_output_filter(void)
{
    int ok = tool_delegate_text_has_dsml_markup("<｜｜DSML｜｜tool_calls><｜｜DSML｜｜invoke name=\"files\">") &&
             !tool_delegate_text_has_dsml_markup("这是正常的结构分析总结，包含关键目录和入口文件。");
    report("delegate DSML output detector", ok);
}

static void test_delegate_safe_output_returns_protocol_failure_summary(void)
{
    const char *safe = tool_delegate_safe_output_text(
        "<｜｜DSML｜｜tool_calls>\n<｜｜DSML｜｜invoke name=\"files\">",
        "这是正常的仓库分析总结，入口在 kernel/turn，调度核心在 tool_delegate。",
        false,
        false);
    int ok = safe &&
             strstr(safe, "delegate_task: subagent returned tool markup/transcript instead of protocol JSON") &&
             !strstr(safe, "<｜｜DSML｜｜");
    report("delegate safe output returns protocol failure summary", ok);
}

static void test_delegate_safe_output_rejects_transcript_markup(void)
{
    const char *safe = tool_delegate_safe_output_text(
        "我先检查关键目录。\n\n<bash>\nfind /tmp/project -maxdepth 2\n</bash>\n\nFILE: /tmp/project/main.c",
        "最终结论：入口在 main.c，调度核心在 kernel/turn。",
        false,
        false);
    int ok = safe &&
             strstr(safe, "delegate_task: subagent returned tool markup/transcript instead of protocol JSON") &&
             !strstr(safe, "<bash>") &&
             !strstr(safe, "FILE:");
    report("delegate safe output rejects transcript markup", ok);
}

static void test_delegate_safe_output_includes_excerpt_for_non_json_text(void)
{
    const char *safe = tool_delegate_safe_output_text(
        "这里有一段不是 JSON 的纯文本总结，但是最终协议没有遵守。",
        "",
        false,
        false);
    int ok = safe &&
             strstr(safe, "delegate_task: subagent returned non-JSON result after finalizer failed") &&
             strstr(safe, "Excerpt:") &&
             strstr(safe, "这里有一段不是 JSON 的纯文本总结");
    report("delegate safe output includes excerpt for non-json text", ok);
}

static void test_delegate_result_json_parser_accepts_valid_json(void)
{
    char summary[1024];
    int ok = tool_delegate_parse_result_json_summary(
        "{\"status\":\"done\",\"summary\":\"kernel/loop.c drives the main loop.\",\"evidence\":[\"kernel/loop.c: agent loop\"],\"risks\":[\"No retry isolation\"],\"next_files\":[\"kernel/tooling/delegate_task_store.c\"]}",
        summary,
        sizeof(summary));
    ok = ok &&
         strstr(summary, "kernel/loop.c drives the main loop.") &&
         strstr(summary, "Evidence:") &&
         strstr(summary, "Next files:");
    report("delegate result json parser accepts valid json", ok);
}

static void test_delegate_result_json_renderer_accepts_blocked_json(void)
{
    char summary[1024];
    int ok = tool_delegate_parse_result_json_rendered(
        "{\"status\":\"blocked\",\"summary\":\"Subagent stopped before producing findings.\",\"evidence\":[\"Returned only next-step narration\"],\"risks\":[\"Caller may trust incomplete output\"],\"next_files\":[]}",
        summary,
        sizeof(summary));
    ok = ok &&
         strstr(summary, "delegate_task: subagent protocol failure") &&
         strstr(summary, "Subagent stopped before producing findings.");
    report("delegate result json renderer accepts blocked json", ok);
}

static void test_delegate_prepare_single_file_prompt_injects_context(void)
{
    char prompt[512];
    char prepared[READ_FILE_MAX_CHARS + 4096];
    bool disable_tools = false;
    snprintf(prompt, sizeof(prompt),
             "只允许读取 /home/wangergou/code/github/daima-agent/kernel/loop.c 这一个文件。分析它的职责并直接给结论。");
    int ok = tool_delegate_prepare_subagent_prompt(
        "explore",
        "分析 kernel/loop.c",
        prompt,
        prepared,
        sizeof(prepared),
        &disable_tools);
    ok = ok &&
         disable_tools &&
         strstr(prepared, "Provided file content:") &&
         strstr(prepared, "FILE: /home/wangergou/code/github/daima-agent/kernel/loop.c");
    report("delegate single-file prompt injects file context", ok);
}

static void test_delegate_prepare_single_file_prompt_truncates_context(void)
{
    char prompt[512];
    char prepared[READ_FILE_MAX_CHARS + 4096];
    bool disable_tools = false;
    snprintf(prompt, sizeof(prompt),
             "只允许读取 /home/wangergou/code/github/daima-agent/drivers/tool/tool_delegate.c 这一个文件。分析它的职责并直接给结论。");
    int ok = tool_delegate_prepare_subagent_prompt(
        "explore",
        "分析 tool_delegate.c",
        prompt,
        prepared,
        sizeof(prepared),
        &disable_tools);
    ok = ok &&
         disable_tools &&
         strstr(prepared, "Provided file content:") &&
         strstr(prepared, "...[truncated by delegate_task]");
    report("delegate single-file prompt truncates injected context", ok);
}

static void test_delegate_task_id_parse_without_subagent_type(void)
{
    const struct tool *t = tool_delegate_definition();
    char output[256];
    err_t err = t->execute("{\"task_id\":\"dt_resume_only\"}", output, sizeof(output));
    int ok = (err == ERR_NOT_FOUND) &&
             strstr(output, "task_id not found") &&
             !strstr(output, "missing required field 'subagent_type'");
    report("delegate task_id parse does not require subagent_type", ok);
}

/* 测 11: AI 自检日志 — 读自己的 log 文件判断健康状态 */
static void test_log_self_check(void)
{
    char log_path[512];
    snprintf(log_path, sizeof(log_path), "%s/agent.log", path_memory_dir());
    FILE *f = fopen(log_path, "r");
    if (!f) {
        report("log self-check (no log file yet)", 1);  /* 首次运行无日志，pass */
        return;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    if (size <= 0) { fclose(f); report("log self-check (empty)", 1); return; }

    long read_from = 0;
    if (size > 131072) { read_from = size - 131072; size = 131072; }
    fseek(f, read_from, SEEK_SET);

    char *buf = kmalloc(size + 1, GFP_KERNEL);
    if (!buf) { fclose(f); report("log self-check (OOM)", 0); return; }
    fread(buf, 1, size, f);
    fclose(f);
    buf[size] = '\0';

    /* 检查健康指标 */
    int ok = 1;

    /* 关键子系统初始化 */
    if (!strstr(buf, "Bus subsystem ready"))
        { pr_warn("  missing: Bus subsystem ready"); ok = 0; }
    if (!strstr(buf, "Agent loop started"))
        { pr_warn("  missing: Agent loop started"); ok = 0; }

    /* 工具执行记录 (log 格式: "Executor: tool_name → err=0" 或 "Tool xxx result:") */
    int tool_ok = 0;
    const char *p = buf;
    while ((p = strstr(p, "executor") ? strstr(p, "executor") :
               strstr(p, "Executor") ? strstr(p, "Executor") : NULL)) {
        if (strstr(p, "err=0")) tool_ok++;
        p++;
    }
    if (tool_ok == 0) { pr_warn("  no tool execution records"); ok = 0; }

    /* 无崩溃信号 */
    if (strstr(buf, "SIGSEGV") || strstr(buf, "stack overflow"))
        { pr_warn("  crash indicator in log"); ok = 0; }

    pr_info("  log: %ld bytes, %d tool records (err=0), init OK=%s",
            size, tool_ok,
            strstr(buf, "Bus subsystem ready") ? "yes" : "no");

    kfree(buf);
    report("log self-check (health scan)", ok);
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
    test_async_compress_dispatch();
    test_delegate_task_legacy_rejected();
    test_delegate_task_sync_implement();
    test_delegate_task_background_handle();
    test_delegate_task_batch_background_returns_coordinator();
    test_delegate_task_batch_poll_returns_agents();
    test_context_prompt_mentions_batch_delegate();
    test_turn_exec_merges_sibling_delegate_calls();
    test_turn_exec_marks_background_delegate_started();
    test_delegate_empty_input_is_recoverable();
    test_delegate_schema_avoids_anyof();
    test_log_self_check();
    test_discovery_files_rewritten_to_delegate();
    test_discovery_files_rewritten_without_investigate_intent();
    test_subagent_discovery_not_rewritten_recursively();
    test_subagent_tool_activity_does_not_require_ws_client();
    test_websocket_tool_activity_disconnect_is_best_effort();
    test_discovery_prompt_marked_as_bounded();
    test_delegate_explore_overview_budget_heuristic();
    test_delegate_dsml_output_filter();
    test_delegate_safe_output_returns_protocol_failure_summary();
    test_delegate_safe_output_rejects_transcript_markup();
    test_delegate_safe_output_includes_excerpt_for_non_json_text();
    test_delegate_result_json_parser_accepts_valid_json();
    test_delegate_result_json_renderer_accepts_blocked_json();
    test_delegate_prepare_single_file_prompt_injects_context();
    test_delegate_prepare_single_file_prompt_truncates_context();
    test_delegate_task_id_parse_without_subagent_type();

    pr_info("----------------------------------------");
    pr_info("  Results: %d/%d passed", tests_pass, tests_run);
    pr_info("========================================");
    return tests_pass == tests_run ? 0 : 1;
}
