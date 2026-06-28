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
#include "drivers/tool/tool_delegate_types.h"
#include "drivers/tool/tool_bus_view.h"
#include "drivers/tool/tool_delegate_overview.h"
#include "drivers/tool/tool_delegate_protocol.h"
#include "drivers/tool/tool_delegate_dependency.h"
#include "drivers/tool/tool_delegate_repo_batch.h"
#include "drivers/tool/tool_delegate_prepare.h"
#include "drivers/tool/tool_delegate_runtime.h"
#include "drivers/tool/tool_delegate_response.h"
#include "drivers/tool/tool_delegate_subagent.h"
#include "drivers/tool/tool_delegate_dispatch.h"
#include "drivers/tool/tool_delegate_summary.h"
#include "delegate/delegate_task_store.h"
#include "delegate/delegate_session_json.h"
#include "delegate/delegate_state_json.h"
#include "drivers/tool/tool_files.h"
#include "drivers/tool/tool_invocation_context.h"
#include "drivers/llm/llm_proxy.h"
#include "drivers/llm/model_fallback.h"
#include "drivers/memory/session_store.h"
#include "drivers/channel/gateway/ws_client.h"
#include "drivers/channel/gateway/ws_http_helpers.h"
#include "kernel/channel/channel_runtime.h"
#include "kernel/context/context_build.h"
#include "kernel/turn/turn_entry.h"
#include "kernel/turn/turn_exec.h"
#include "kernel/turn/turn_gate.h"
#include "kernel/turn/turn_common.h"
#include "kernel/turn/turn_context.h"
#include "kernel/turn/turn_interview.h"
#include "interactive.h"
#include "intent.h"
#include "interview.h"
#include "kernel/tooling/tool_guard.h"
#include "delegate/delegate_turn_directive.h"
#include "delegate/delegate_parent_wake.h"
#include "drivers/tool/tool_runtime.h"
#include "tool_notify.h"
#include "paths.h"
#include "workspace_probe.h"
#include "arch/host/portability.h"
#include "cjson.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>

#define TEST_OUTPUT_SIZE 65536
#define PASS "✅ PASS"
#define FAIL "❌ FAIL"

static int tests_run = 0;
static int tests_pass = 0;
static self_test_log_probe_t s_self_test_log_probe;
static bool s_self_test_log_probe_pending;

typedef struct { char name[64]; int ok; } test_result_t;
static test_result_t s_results[256];
static int s_result_count = 0;

typedef struct {
    delegate_coordinator_record_t *left;
    delegate_coordinator_record_t *right;
} delegate_two_snapshot_wait_t;

typedef struct {
    delegate_coordinator_record_t *left;
    delegate_coordinator_record_t *middle;
    delegate_coordinator_record_t *right;
} delegate_three_snapshot_wait_t;

typedef struct {
    char coordinator_id[DELEGATE_COORDINATOR_ID_LEN];
    delegate_coordinator_record_t *snapshot;
} delegate_single_snapshot_wait_t;

static bool wait_for_delegate_lifecycle_snapshot(bool (*predicate)(void *ctx),
                                                 void *ctx,
                                                 int timeout_ms);
static bool predicate_delegate_fair_launch(void *ctx);
static bool predicate_delegate_per_coordinator_cap(void *ctx);
static bool predicate_delegate_per_parent_cap(void *ctx);
static bool predicate_delegate_blocked_coordinator_cap(void *ctx);
static bool predicate_delegate_blocked_parent_cap(void *ctx);
static bool predicate_delegate_long_prompt_schedulable(void *ctx);

static void build_workspace_opencode_path(char *buf, size_t size, const char *suffix)
{
    if (!buf || size == 0) {
        return;
    }
    if (!suffix || !suffix[0]) {
        snprintf(buf, size, "%s/opencode", path_workspace_dir());
        return;
    }
    snprintf(buf, size, "%s/opencode/%s", path_workspace_dir(), suffix);
}

static void build_self_test_probe_root(char *buf, size_t size)
{
    if (!buf || size == 0) {
        return;
    }
    snprintf(buf, size, "%s/self_test_repo_probes", path_memory_dir());
}

static void build_workspace_opencode_probe_path(char *buf, size_t size, const char *suffix)
{
    char root[PATH_MAX];

    if (!buf || size == 0) {
        return;
    }
    build_self_test_probe_root(root, sizeof(root));
    if (!suffix || !suffix[0]) {
        snprintf(buf, size, "%s/opencode_probe", root);
        return;
    }
    snprintf(buf, size, "%s/opencode_probe/%s", root, suffix);
}

static void report(const char *name, int ok)
{
    tests_run++;
    if (ok) tests_pass++;
    if (s_result_count < (int)ARRAY_SIZE(s_results)) {
        strscpy(s_results[s_result_count].name, name, sizeof(s_results[s_result_count].name));
        s_results[s_result_count].ok = ok;
        s_result_count++;
    }
    pr_info("[TEST] %s: %s", ok ? PASS : FAIL, name);
}

static int self_test_run_process_quiet(const char *program, char *const argv[])
{
    if (!program || !argv) {
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        return -1;
    }

    if (pid == 0) {
        execvp(program, argv);
        _exit(127);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0 || !WIFEXITED(status)) {
        return -1;
    }
    return WEXITSTATUS(status);
}

static ssize_t self_test_recv_all_http_response(int fd, char *buf, size_t buf_size)
{
    size_t off = 0;
    char *header_end = NULL;
    size_t target_size = 0;
    bool first_read = true;

    if (fd < 0 || !buf || buf_size == 0) {
        return -1;
    }

    while (off + 1 < buf_size) {
        ssize_t n;

        if (!first_read) {
            struct pollfd pfd;
            int poll_rc;

            memset(&pfd, 0, sizeof(pfd));
            pfd.fd = fd;
            pfd.events = POLLIN;
            poll_rc = poll(&pfd, 1, 50);
            if (poll_rc <= 0 || !(pfd.revents & POLLIN)) {
                break;
            }
        }

        n = recv(fd, buf + off, buf_size - off - 1, 0);
        if (n < 0) {
            return -1;
        }
        if (n == 0) {
            break;
        }
        first_read = false;
        off += (size_t)n;
        buf[off] = '\0';

        if (!header_end) {
            header_end = strstr(buf, "\r\n\r\n");
            if (header_end) {
                size_t header_size = (size_t)(header_end - buf) + 4;
                const char *content_length = strstr(buf, "Content-Length:");
                unsigned long body_len = 0;

                if (content_length) {
                    content_length += strlen("Content-Length:");
                    while (*content_length == ' ') {
                        content_length++;
                    }
                    body_len = strtoul(content_length, NULL, 10);
                }
                target_size = header_size + (size_t)body_len;
                if (target_size == 0) {
                    target_size = header_size;
                }
            }
        }

        if (target_size > 0 && off >= target_size) {
            break;
        }
    }

    buf[off] = '\0';
    return (ssize_t)off;
}

void agent_self_test_set_log_probe(const self_test_log_probe_t *probe)
{
    if (!probe) {
        memset(&s_self_test_log_probe, 0, sizeof(s_self_test_log_probe));
        return;
    }
    s_self_test_log_probe = *probe;
}

void agent_self_test_set_log_probe_pending(bool pending)
{
    s_self_test_log_probe_pending = pending;
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
    {
        cJSON *log_probe = cJSON_CreateObject();
        cJSON_AddBoolToObject(log_probe, "marker_found", s_self_test_log_probe.marker_found);
        cJSON_AddNumberToObject(log_probe, "attach_task_hits", s_self_test_log_probe.attach_task_hits);
        cJSON_AddNumberToObject(log_probe, "launch_candidate_hits", s_self_test_log_probe.launch_candidate_hits);
        cJSON_AddNumberToObject(log_probe, "restore_queued_hits", s_self_test_log_probe.restore_queued_hits);
        cJSON_AddBoolToObject(log_probe, "multi_subagent_confirmed",
                              s_self_test_log_probe.multi_subagent_confirmed);
        cJSON_AddBoolToObject(log_probe, "pending", s_self_test_log_probe_pending);
        cJSON_AddItemToObject(root, "log_probe", log_probe);
    }

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

static void test_intent_action_request_heuristic(void)
{
    int ok = intent_gate_text_looks_like_action_request_for_test("请直接实现这个需求，但我还没想好细节，你先做") &&
             intent_gate_text_looks_like_action_request_for_test("修一下这个功能") &&
             !intent_gate_text_looks_like_action_request_for_test("这段代码是什么意思？");
    report("intent heuristic keeps action-like requests out of QA", ok);
}

static void test_intent_fallback_keeps_action_requests_as_implement(void)
{
    enum intent intent = intent_gate_fallback_for_text(
        "帮我改一下 /home/wangergou/code/github/daima-agent ，但我还没想好改哪个模块，你先直接开始。");
    int ok = (intent == INTENT_IMPLEMENT);
    report("intent fallback keeps vague action request as implement", ok);
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

/* 测 4: 记忆核 fire-and-forget 会话保存 */
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
    int ok = (core_recv(CORE_SCHEDULER, &reply, 200) != 0);
    if (!ok) {
        kfree(reply.payload);
        kfree(reply.result);
    }
    report("memory queue save_session is fire-and-forget", ok);
}

static void test_host_portability_runtime(void)
{
    char exe_dir[PATH_MAX];
    memset(exe_dir, 0, sizeof(exe_dir));

    int exe_ok = host_get_executable_dir(exe_dir, sizeof(exe_dir)) &&
                 exe_dir[0] == '/';
    if (!exe_ok) {
        pr_warn("  failed to resolve executable dir");
    }

    size_t free_mem = host_platform_free_memory();
    int mem_ok = (free_mem > 0);
    if (!mem_ok) {
        pr_warn("  failed to query platform free memory");
    }

    report("host portability resolves executable dir and free memory", exe_ok && mem_ok);
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
    int ok = (core_recv(CORE_SCHEDULER, &reply, 200) != 0);
    kfree(reply.payload);
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
        "\"description\":\"scan repo root\","
        "\"prompt\":\"分析 /home/wangergou/code/github/daima-agent 的目录结构和关键模块，给出代表性文件。\","
        "\"subagent_type\":\"explore\","
        "\"run_in_background\":false"
        "}";

    const struct tool *t = tool_delegate_definition();
    err_t err = t->execute(input, output, sizeof(output));
    int ok = (err == 0);
    if (ok) {
        char summary[4096];
        bool extracted;
        memset(summary, 0, sizeof(summary));
        extracted = tool_delegate_extract_sync_final_output(output, summary, sizeof(summary));
        ok = strstr(output, "\"status\":\"done\"") &&
             strstr(output, "\"subagent_type\":\"explore\"") &&
             strstr(output, "\"description\":\"scan repo root\"") &&
             extracted &&
             summary[0];
        pr_info("  sync implement extracted=%d summary_len=%zu output_len=%zu result: %.160s...",
                extracted ? 1 : 0,
                strlen(summary),
                strlen(output),
                output);
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
        "\"description\":\"scan kernel bg\","
        "\"prompt\":\"分析 /home/wangergou/code/github/daima-agent/kernel 的目录结构和关键模块，说明入口与主链。\","
        "\"subagent_type\":\"explore\","
        "\"run_in_background\":true"
        "}";

    const struct tool *t = tool_delegate_definition();
    err_t err = t->execute(input, output, sizeof(output));
    int ok = (err == 0) &&
             strstr(output, "\"task_id\":\"dt_") &&
             strstr(output, "\"session_id\":\"delegate_sync_");
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
            "\"prompt\":\"分析 /home/wangergou/code/github/daima-agent/kernel 的目录结构和关键模块，说明入口与主链。\","
            "\"subagent_type\":\"explore\""
          "},"
          "{"
            "\"description\":\"check docs\","
            "\"prompt\":\"分析 /home/wangergou/code/github/daima-agent/docs 的目录结构、关键文档和建议继续看的文件。\","
            "\"subagent_type\":\"explore\""
          "}"
        "]"
        "}";

    const struct tool *t = tool_delegate_definition();
    err_t err = t->execute(input, output, sizeof(output));
    int ok = (err == 0) &&
             strstr(output, "\"coordinator_id\":\"dc_") &&
             strstr(output, "\"task_id\":\"dt_") &&
             strstr(output, "\"session_id\":\"delegate_sync_") &&
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
            "\"prompt\":\"分析 /home/wangergou/code/github/daima-agent/docs 的目录结构、关键文档和建议继续看的文件。\","
            "\"subagent_type\":\"explore\""
          "},"
          "{"
            "\"description\":\"repo scan\","
            "\"prompt\":\"分析 /home/wangergou/code/github/daima-agent/kernel 的目录结构和关键模块，说明入口与主链。\","
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
             strstr(poll_output, "\"session_id\":\"delegate_sync_") &&
             strstr(poll_output, "\"status\":\"");
        pr_info("  batch delegate poll: err=%d output=%.240s", err, poll_output);
    }

    report("delegate_task batch poll returns agents", ok);
}

static void test_delegate_task_parent_registry_list(void)
{
    char start_output[8192];
    char list_output[12288];
    memset(start_output, 0, sizeof(start_output));
    memset(list_output, 0, sizeof(list_output));
    delegate_task_store_reset_for_test();

    struct message msg;
    memset(&msg, 0, sizeof(msg));
    strscpy(msg.channel, CHAN_WEBSOCKET, sizeof(msg.channel));
    strscpy(msg.chat_id, "test_parent_registry", sizeof(msg.chat_id));
    strscpy(msg.source, "self_test", sizeof(msg.source));
    msg.content = strdup("delegate parent registry list");

    llm_tool_call_t call;
    memset(&call, 0, sizeof(call));
    strscpy(call.id, "tool_delegate_list", sizeof(call.id));
    strscpy(call.name, "delegate_task", sizeof(call.name));
    call.input = strdup(
        "{"
        "\"tasks\":["
          "{"
            "\"description\":\"scan kernel\","
            "\"prompt\":\"分析 /home/wangergou/code/github/daima-agent/kernel 的目录结构和关键模块，说明入口与主链。\","
            "\"subagent_type\":\"explore\""
          "},"
          "{"
            "\"description\":\"scan drivers\","
            "\"prompt\":\"分析 /home/wangergou/code/github/daima-agent/drivers/tool 的目录结构和关键模块，说明代表性文件。\","
            "\"subagent_type\":\"explore\""
          "}"
        "]"
        "}");
    call.input_len = strlen(call.input);

    tool_runtime_result_t rt;
    err_t err = tool_runtime_execute_call(&call, &msg, start_output, sizeof(start_output), &rt);
    int ok = (err == 0) &&
             strstr(start_output, "\"coordinator_id\":\"dc_") &&
             strstr(start_output, "\"task_id\":\"dt_");
    free(call.input);

    if (ok) {
        memset(&call, 0, sizeof(call));
        strscpy(call.id, "tool_delegate_list_2", sizeof(call.id));
        strscpy(call.name, "delegate_task", sizeof(call.name));
        call.input = strdup("{\"action\":\"list\"}");
        call.input_len = strlen(call.input);
        err = tool_runtime_execute_call(&call, &msg, list_output, sizeof(list_output), &rt);
        ok = (err == 0) &&
             strstr(list_output, "\"action\":\"list\"") &&
             strstr(list_output, "\"scope\":\"parent\"") &&
             strstr(list_output, "\"chat_id\":\"test_parent_registry\"") &&
             strstr(list_output, "\"coordinator_id\":\"dc_") &&
             strstr(list_output, "\"task_id\":\"dt_") &&
             strstr(list_output, "\"session_id\":\"delegate_sync_") &&
             strstr(list_output, "\"coordinators\":[") &&
             strstr(list_output, "\"tasks\":[");
        pr_info("  parent registry list: err=%d output=%.320s", err, list_output);
        free(call.input);
    }

    free(msg.content);
    report("delegate_task parent registry list", ok);
}

static void test_delegate_task_store_done_update_after_parent_response(void)
{
    delegate_task_store_reset_for_test();

    err_t err = delegate_task_store_start_coordinator("dc_store_done", "chat_store_done", "", "", "parallel");
    int ok = (err == 0);
    if (ok) {
        ok = delegate_task_store_start("dt_store_done", "dc_store_done", "delegate_sync_store", "explore",
                                       "", "store done propagation", "prompt", "deepseek-v4-pro", "kernel/turn", "subsystem", "turn_execution", NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_attach_task("dc_store_done", "dt_store_done") == 0;
    }

    delegate_coordinator_record_t records[4];
    memset(records, 0, sizeof(records));
    if (ok) {
        ok = !delegate_task_store_poll_updates(records, 4);
    }
    if (ok) {
        ok = delegate_task_store_mark_parent_response_sent("chat_store_done") == 0;
    }
    if (ok) {
        memset(records, 0, sizeof(records));
        ok = delegate_task_store_poll_updates(records, 4) &&
             strcmp(records[0].status, "running") == 0;
    }
    if (ok) {
        ok = delegate_task_store_complete("dt_store_done", "done summary", "", false) == 0;
    }
    if (ok) {
        memset(records, 0, sizeof(records));
        ok = delegate_task_store_poll_updates(records, 4) &&
             strcmp(records[0].status, "done") == 0 &&
             records[0].completed_count == records[0].agent_count;
    }

    report("delegate task store emits done update after parent response", ok);
}

static void test_delegate_task_store_staged_counts(void)
{
    delegate_task_store_reset_for_test();

    int ok = delegate_task_store_start_coordinator("dc_stage", "chat_stage", "tr_stage", "stage-team", "staged") == 0;
    if (ok) {
        ok = delegate_task_store_plan("dt_stage_a", "dc_stage", "delegate_sync_stage_a", "explore",
                                      "map-kernel", "map kernel", "prompt a", "deepseek-v4-pro",
                                      "kernel", "subsystem", "execution_kernel", "", NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_attach_task("dc_stage", "dt_stage_a") == 0;
    }
    if (ok) {
        ok = delegate_task_store_plan("dt_stage_b", "dc_stage", "delegate_sync_stage_b", "oracle",
                                      "review-kernel", "review kernel", "prompt b", "deepseek-v4-pro",
                                      "kernel", "subsystem", "coordination", "map-kernel", NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_attach_task("dc_stage", "dt_stage_b") == 0;
    }

    delegate_coordinator_record_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    if (ok) {
        ok = delegate_task_store_snapshot_coordinator("dc_stage", &snapshot) == 0 &&
             strcmp(snapshot.team_run_id, "tr_stage") == 0 &&
             strcmp(snapshot.team_name, "stage-team") == 0 &&
             strcmp(snapshot.dispatch_mode, "staged") == 0 &&
             snapshot.queued_count == 2;
    }
    if (ok) {
        ok = delegate_task_store_mark_running("dt_stage_a") == 0;
    }
    if (ok) {
        ok = delegate_task_store_snapshot_coordinator("dc_stage", &snapshot) == 0 &&
             snapshot.running_count == 1 &&
             snapshot.queued_count == 1;
    }
    if (ok) {
        ok = delegate_task_store_complete("dt_stage_a", "done a", "", false) == 0;
    }
    if (ok) {
        ok = delegate_task_store_mark_running("dt_stage_b") == 0;
    }
    if (ok) {
        ok = delegate_task_store_snapshot_coordinator("dc_stage", &snapshot) == 0 &&
             snapshot.completed_count == 1 &&
             snapshot.running_count == 1 &&
             snapshot.queued_count == 0 &&
             strcmp(snapshot.agents[1].depends_on, "map-kernel") == 0 &&
             strcmp(snapshot.agents[1].task_key, "review-kernel") == 0;
    }
    if (!ok) {
        pr_info("  stage diag: status=%s completed=%d running=%d queued=%d agent0_status=%s agent1_status=%s agent0_key=%s agent1_key=%s",
                snapshot.status,
                snapshot.completed_count,
                snapshot.running_count,
                snapshot.queued_count,
                snapshot.agents[0].status,
                snapshot.agents[1].status,
                snapshot.agents[0].task_key,
                snapshot.agents[1].task_key);
    }

    report("delegate task store tracks staged queued/running counts", ok);
}

static void test_delegate_background_launch_respects_running_budget(void)
{
    delegate_coordinator_record_t snapshot;
    const char *saved = getenv("DELEGATE_BG_MAX_CONCURRENCY");
    char saved_copy[32];
    int ok;

    memset(&snapshot, 0, sizeof(snapshot));
    memset(saved_copy, 0, sizeof(saved_copy));
    if (saved && saved[0]) {
        strscpy(saved_copy, saved, sizeof(saved_copy));
    }

    delegate_task_store_reset_for_test();
    setenv("DELEGATE_BG_MAX_CONCURRENCY", "1", 1);

    ok = delegate_task_store_start_coordinator("dc_budget", "chat_budget", "tr_budget", "budget-team", "parallel") == 0;
    if (ok) {
        ok = delegate_task_store_plan("dt_budget_running", "dc_budget", "delegate_sync_budget_running", "explore",
                                      "budget-running", "running child", "prompt a", "deepseek-v4-pro",
                                      "kernel/a", "subsystem", "focus_a", NULL, NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_attach_task("dc_budget", "dt_budget_running") == 0;
    }
    if (ok) {
        ok = delegate_task_store_plan("dt_budget_queued", "dc_budget", "delegate_sync_budget_queued", "explore",
                                      "budget-queued", "queued child", "prompt b", "deepseek-v4-pro",
                                      "kernel/b", "subsystem", "focus_b", NULL, NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_attach_task("dc_budget", "dt_budget_queued") == 0;
    }
    if (ok) {
        ok = delegate_task_store_mark_running("dt_budget_running") == 0;
    }
    if (ok) {
        ok = tool_delegate_launch_ready_background_subagents("dc_budget", "chat_budget") == 0;
    }
    if (ok) {
        ok = delegate_task_store_snapshot_coordinator("dc_budget", &snapshot) == 0 &&
             snapshot.running_count == 1 &&
             snapshot.queued_count == 1 &&
             strcmp(snapshot.agents[1].status, "queued") == 0;
    }

    if (saved_copy[0]) {
        setenv("DELEGATE_BG_MAX_CONCURRENCY", saved_copy, 1);
    } else {
        unsetenv("DELEGATE_BG_MAX_CONCURRENCY");
    }

    report("delegate background launch respects running budget", ok);
}

static void test_delegate_background_launch_keeps_capacity_for_real_batch(void)
{
    delegate_coordinator_record_t snapshot;
    int ok = 1;
    char app_path[PATH_MAX];
    char cli_path[PATH_MAX];
    char session_ui_path[PATH_MAX];

    memset(&snapshot, 0, sizeof(snapshot));
    delegate_task_store_reset_for_test();
    build_workspace_opencode_path(app_path, sizeof(app_path), "packages/app");
    build_workspace_opencode_path(cli_path, sizeof(cli_path), "packages/cli");
    build_workspace_opencode_path(session_ui_path, sizeof(session_ui_path), "packages/session-ui");

    for (int i = 0; ok && i < 24; i++) {
        char coordinator_id[32];
        char task_id[32];
        char session_id[32];
        char description[64];

        snprintf(coordinator_id, sizeof(coordinator_id), "dc_hist_%d", i);
        snprintf(task_id, sizeof(task_id), "dt_hist_%d", i);
        snprintf(session_id, sizeof(session_id), "delegate_sync_hist_%d", i);
        snprintf(description, sizeof(description), "historical task %d", i);

        ok = delegate_task_store_start_coordinator(coordinator_id,
                                                   "chat_hist",
                                                   "tr_hist",
                                                   "history-team",
                                                   "parallel") == 0;
        if (ok) {
            ok = delegate_task_store_start(task_id,
                                           coordinator_id,
                                           session_id,
                                           "explore",
                                           "history",
                                           description,
                                           "history prompt",
                                           "deepseek-v4-pro",
                                           "/tmp/history",
                                           "subsystem",
                                           "history_focus",
                                           NULL) == 0;
        }
        if (ok) {
            ok = delegate_task_store_attach_task(coordinator_id, task_id) == 0;
        }
        if (ok) {
            ok = delegate_task_store_complete(task_id, "history summary", "", false) == 0;
        }
    }

    if (ok) {
        ok = delegate_task_store_start_coordinator("dc_real_batch",
                                                   "chat_real_batch",
                                                   "tr_real_batch",
                                                   "real-batch-team",
                                                   "parallel") == 0;
    }
    if (ok) {
        ok = delegate_task_store_plan("dt_real_app",
                                      "dc_real_batch",
                                      "delegate_sync_real_app",
                                      "explore",
                                      "app",
                                      "analyze app",
                                      "prompt app",
                                      "deepseek-v4-pro",
                                      app_path,
                                      "subsystem",
                                      "local_overview",
                                      NULL,
                                      NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_attach_task("dc_real_batch", "dt_real_app") == 0;
    }
    if (ok) {
        ok = delegate_task_store_plan("dt_real_cli",
                                      "dc_real_batch",
                                      "delegate_sync_real_cli",
                                      "explore",
                                      "cli",
                                      "analyze cli",
                                      "prompt cli",
                                      "deepseek-v4-pro",
                                      cli_path,
                                      "subsystem",
                                      "local_overview",
                                      NULL,
                                      NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_attach_task("dc_real_batch", "dt_real_cli") == 0;
    }
    if (ok) {
        ok = delegate_task_store_plan("dt_real_session_ui",
                                      "dc_real_batch",
                                      "delegate_sync_real_session_ui",
                                      "explore",
                                      "session-ui",
                                      "analyze session-ui",
                                      "prompt session-ui",
                                      "deepseek-v4-pro",
                                      session_ui_path,
                                      "subsystem",
                                      "local_overview",
                                      NULL,
                                      NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_attach_task("dc_real_batch", "dt_real_session_ui") == 0;
    }
    if (ok) {
        ok = delegate_task_store_snapshot_coordinator("dc_real_batch", &snapshot) == 0 &&
             snapshot.agent_count == 3 &&
             snapshot.queued_count == 3 &&
             strcmp(snapshot.status, "queued") == 0;
    }

    report("delegate background launch keeps capacity for real multi-subagent batch", ok);
}

static void test_delegate_lifecycle_runtime_launches_across_coordinators_fairly(void)
{
    delegate_coordinator_record_t left;
    delegate_coordinator_record_t right;
    delegate_two_snapshot_wait_t wait_state;
    const char *saved = getenv("DELEGATE_BG_MAX_CONCURRENCY");
    const char *saved_delay = getenv("DELEGATE_BG_TEST_TASK_DELAY_MS");
    char saved_copy[32];
    char saved_delay_copy[32];
    char app_path[PATH_MAX];
    char cli_path[PATH_MAX];
    char session_ui_path[PATH_MAX];
    char core_path[PATH_MAX];
    int ok = 1;

    memset(&left, 0, sizeof(left));
    memset(&right, 0, sizeof(right));
    memset(&wait_state, 0, sizeof(wait_state));
    memset(saved_copy, 0, sizeof(saved_copy));
    memset(saved_delay_copy, 0, sizeof(saved_delay_copy));
    wait_state.left = &left;
    wait_state.right = &right;
    build_workspace_opencode_path(app_path, sizeof(app_path), "packages/app");
    build_workspace_opencode_path(cli_path, sizeof(cli_path), "packages/cli");
    build_workspace_opencode_path(session_ui_path, sizeof(session_ui_path), "packages/session-ui");
    build_workspace_opencode_path(core_path, sizeof(core_path), "packages/core");
    if (saved && saved[0]) {
        strscpy(saved_copy, saved, sizeof(saved_copy));
    }
    if (saved_delay && saved_delay[0]) {
        strscpy(saved_delay_copy, saved_delay, sizeof(saved_delay_copy));
    }

    delegate_task_store_reset_for_test();
    setenv("DELEGATE_BG_MAX_CONCURRENCY", "2", 1);
    setenv("DELEGATE_BG_TEST_TASK_DELAY_MS", "300", 1);

    ok = delegate_task_store_start_coordinator("dc_fair_a", "chat_fair_a", "tr_fair_a", "fair-a", "parallel") == 0;
    if (ok) {
        ok = delegate_task_store_start_coordinator("dc_fair_b", "chat_fair_b", "tr_fair_b", "fair-b", "parallel") == 0;
    }

    if (ok) {
        ok = delegate_task_store_plan("dt_fair_app", "dc_fair_a", "delegate_sync_fair_app", "explore",
                                      "fair-app", "fair app child", "analyze app module", "deepseek-v4-pro",
                                      app_path, "subsystem", "focus_app", NULL, NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_attach_task("dc_fair_a", "dt_fair_app") == 0;
    }
    if (ok) {
        ok = delegate_task_store_plan("dt_fair_cli", "dc_fair_a", "delegate_sync_fair_cli", "explore",
                                      "fair-cli", "fair cli child", "analyze cli module", "deepseek-v4-pro",
                                      cli_path, "subsystem", "focus_cli", NULL, NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_attach_task("dc_fair_a", "dt_fair_cli") == 0;
    }
    if (ok) {
        ok = delegate_task_store_plan("dt_fair_session_ui", "dc_fair_b", "delegate_sync_fair_session_ui", "explore",
                                      "fair-session-ui", "fair session-ui child", "analyze session-ui module", "deepseek-v4-pro",
                                      session_ui_path, "subsystem", "focus_session_ui", NULL, NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_attach_task("dc_fair_b", "dt_fair_session_ui") == 0;
    }
    if (ok) {
        ok = delegate_task_store_plan("dt_fair_core", "dc_fair_b", "delegate_sync_fair_core", "explore",
                                      "fair-core", "fair core child", "analyze core module", "deepseek-v4-pro",
                                      core_path, "subsystem", "focus_core", NULL, NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_attach_task("dc_fair_b", "dt_fair_core") == 0;
    }
    if (ok) {
        ok = delegate_launch_ready_background_subagents_for_runtime() == 0;
    }
    if (ok) {
        ok = wait_for_delegate_lifecycle_snapshot(predicate_delegate_fair_launch,
                                                  &wait_state,
                                                  1000);
    }

    if (saved_copy[0]) {
        setenv("DELEGATE_BG_MAX_CONCURRENCY", saved_copy, 1);
    } else {
        unsetenv("DELEGATE_BG_MAX_CONCURRENCY");
    }
    if (saved_delay_copy[0]) {
        setenv("DELEGATE_BG_TEST_TASK_DELAY_MS", saved_delay_copy, 1);
    } else {
        unsetenv("DELEGATE_BG_TEST_TASK_DELAY_MS");
    }

    report("delegate lifecycle runtime launches across coordinators fairly", ok);
}

static void test_delegate_lifecycle_runtime_respects_per_coordinator_cap(void)
{
    delegate_coordinator_record_t heavy;
    delegate_coordinator_record_t light;
    delegate_two_snapshot_wait_t wait_state;
    const char *saved_global = getenv("DELEGATE_BG_MAX_CONCURRENCY");
    const char *saved_per = getenv("DELEGATE_BG_MAX_PER_COORDINATOR");
    const char *saved_delay = getenv("DELEGATE_BG_TEST_TASK_DELAY_MS");
    char saved_global_copy[32];
    char saved_per_copy[32];
    char saved_delay_copy[32];
    char app_path[PATH_MAX];
    char cli_path[PATH_MAX];
    char session_ui_path[PATH_MAX];
    char core_path[PATH_MAX];
    int ok = 1;

    memset(&heavy, 0, sizeof(heavy));
    memset(&light, 0, sizeof(light));
    memset(&wait_state, 0, sizeof(wait_state));
    memset(saved_global_copy, 0, sizeof(saved_global_copy));
    memset(saved_per_copy, 0, sizeof(saved_per_copy));
    memset(saved_delay_copy, 0, sizeof(saved_delay_copy));
    wait_state.left = &heavy;
    wait_state.right = &light;
    build_workspace_opencode_path(app_path, sizeof(app_path), "packages/app");
    build_workspace_opencode_path(cli_path, sizeof(cli_path), "packages/cli");
    build_workspace_opencode_path(session_ui_path, sizeof(session_ui_path), "packages/session-ui");
    build_workspace_opencode_path(core_path, sizeof(core_path), "packages/core");
    if (saved_global && saved_global[0]) {
        strscpy(saved_global_copy, saved_global, sizeof(saved_global_copy));
    }
    if (saved_per && saved_per[0]) {
        strscpy(saved_per_copy, saved_per, sizeof(saved_per_copy));
    }
    if (saved_delay && saved_delay[0]) {
        strscpy(saved_delay_copy, saved_delay, sizeof(saved_delay_copy));
    }

    delegate_task_store_reset_for_test();
    setenv("DELEGATE_BG_MAX_CONCURRENCY", "4", 1);
    setenv("DELEGATE_BG_MAX_PER_COORDINATOR", "1", 1);
    setenv("DELEGATE_BG_TEST_TASK_DELAY_MS", "300", 1);

    ok = delegate_task_store_start_coordinator("dc_cap_heavy", "chat_cap_heavy", "tr_cap_heavy", "cap-heavy", "parallel") == 0;
    if (ok) {
        ok = delegate_task_store_start_coordinator("dc_cap_light", "chat_cap_light", "tr_cap_light", "cap-light", "parallel") == 0;
    }

    if (ok) {
        ok = delegate_task_store_plan("dt_cap_app", "dc_cap_heavy", "delegate_sync_cap_app", "explore",
                                      "cap-app", "cap heavy app child", "analyze app module", "deepseek-v4-pro",
                                      app_path, "subsystem", "focus_app", NULL, NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_attach_task("dc_cap_heavy", "dt_cap_app") == 0;
    }
    if (ok) {
        ok = delegate_task_store_plan("dt_cap_cli", "dc_cap_heavy", "delegate_sync_cap_cli", "explore",
                                      "cap-cli", "cap heavy cli child", "analyze cli module", "deepseek-v4-pro",
                                      cli_path, "subsystem", "focus_cli", NULL, NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_attach_task("dc_cap_heavy", "dt_cap_cli") == 0;
    }
    if (ok) {
        ok = delegate_task_store_plan("dt_cap_session_ui", "dc_cap_heavy", "delegate_sync_cap_session_ui", "explore",
                                      "cap-session-ui", "cap heavy session-ui child", "analyze session-ui module", "deepseek-v4-pro",
                                      session_ui_path, "subsystem", "focus_session_ui", NULL, NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_attach_task("dc_cap_heavy", "dt_cap_session_ui") == 0;
    }
    if (ok) {
        ok = delegate_task_store_plan("dt_cap_core", "dc_cap_light", "delegate_sync_cap_core", "explore",
                                      "cap-core", "cap light core child", "analyze core module", "deepseek-v4-pro",
                                      core_path, "subsystem", "focus_core", NULL, NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_attach_task("dc_cap_light", "dt_cap_core") == 0;
    }

    if (ok) {
        ok = delegate_launch_ready_background_subagents_for_runtime() == 0;
    }
    if (ok) {
        ok = wait_for_delegate_lifecycle_snapshot(predicate_delegate_per_coordinator_cap,
                                                  &wait_state,
                                                  1000);
    }

    if (saved_global_copy[0]) {
        setenv("DELEGATE_BG_MAX_CONCURRENCY", saved_global_copy, 1);
    } else {
        unsetenv("DELEGATE_BG_MAX_CONCURRENCY");
    }
    if (saved_per_copy[0]) {
        setenv("DELEGATE_BG_MAX_PER_COORDINATOR", saved_per_copy, 1);
    } else {
        unsetenv("DELEGATE_BG_MAX_PER_COORDINATOR");
    }
    if (saved_delay_copy[0]) {
        setenv("DELEGATE_BG_TEST_TASK_DELAY_MS", saved_delay_copy, 1);
    } else {
        unsetenv("DELEGATE_BG_TEST_TASK_DELAY_MS");
    }

    report("delegate lifecycle runtime respects per-coordinator cap", ok);
}

static void test_delegate_lifecycle_runtime_respects_per_parent_cap(void)
{
    delegate_coordinator_record_t left;
    delegate_coordinator_record_t right;
    delegate_coordinator_record_t other;
    delegate_three_snapshot_wait_t wait_state;
    const char *saved_global = getenv("DELEGATE_BG_MAX_CONCURRENCY");
    const char *saved_parent = getenv("DELEGATE_BG_MAX_PER_PARENT");
    const char *saved_delay = getenv("DELEGATE_BG_TEST_TASK_DELAY_MS");
    char saved_global_copy[32];
    char saved_parent_copy[32];
    char saved_delay_copy[32];
    char app_path[PATH_MAX];
    char cli_path[PATH_MAX];
    char session_ui_path[PATH_MAX];
    char core_path[PATH_MAX];
    char sdk_path[PATH_MAX];
    int ok = 1;

    memset(&left, 0, sizeof(left));
    memset(&right, 0, sizeof(right));
    memset(&other, 0, sizeof(other));
    memset(&wait_state, 0, sizeof(wait_state));
    memset(saved_global_copy, 0, sizeof(saved_global_copy));
    memset(saved_parent_copy, 0, sizeof(saved_parent_copy));
    memset(saved_delay_copy, 0, sizeof(saved_delay_copy));
    wait_state.left = &left;
    wait_state.middle = &right;
    wait_state.right = &other;
    build_workspace_opencode_path(app_path, sizeof(app_path), "packages/app");
    build_workspace_opencode_path(cli_path, sizeof(cli_path), "packages/cli");
    build_workspace_opencode_path(session_ui_path, sizeof(session_ui_path), "packages/session-ui");
    build_workspace_opencode_path(core_path, sizeof(core_path), "packages/core");
    build_workspace_opencode_path(sdk_path, sizeof(sdk_path), "packages/sdk");
    if (saved_global && saved_global[0]) {
        strscpy(saved_global_copy, saved_global, sizeof(saved_global_copy));
    }
    if (saved_parent && saved_parent[0]) {
        strscpy(saved_parent_copy, saved_parent, sizeof(saved_parent_copy));
    }
    if (saved_delay && saved_delay[0]) {
        strscpy(saved_delay_copy, saved_delay, sizeof(saved_delay_copy));
    }

    delegate_task_store_reset_for_test();
    setenv("DELEGATE_BG_MAX_CONCURRENCY", "4", 1);
    setenv("DELEGATE_BG_MAX_PER_PARENT", "2", 1);
    setenv("DELEGATE_BG_TEST_TASK_DELAY_MS", "300", 1);

    ok = delegate_task_store_start_coordinator("dc_parent_a1", "chat_parent_a", "tr_parent_a", "parent-a", "parallel") == 0;
    if (ok) {
        ok = delegate_task_store_start_coordinator("dc_parent_a2", "chat_parent_a", "tr_parent_a", "parent-a", "parallel") == 0;
    }
    if (ok) {
        ok = delegate_task_store_start_coordinator("dc_parent_b1", "chat_parent_b", "tr_parent_b", "parent-b", "parallel") == 0;
    }

    if (ok) {
        ok = delegate_task_store_plan("dt_parent_app", "dc_parent_a1", "delegate_sync_parent_app", "explore",
                                      "parent-app", "parent app child", "analyze app module", "deepseek-v4-pro",
                                      app_path, "subsystem", "focus_app", NULL, NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_attach_task("dc_parent_a1", "dt_parent_app") == 0;
    }
    if (ok) {
        ok = delegate_task_store_plan("dt_parent_cli", "dc_parent_a1", "delegate_sync_parent_cli", "explore",
                                      "parent-cli", "parent cli child", "analyze cli module", "deepseek-v4-pro",
                                      cli_path, "subsystem", "focus_cli", NULL, NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_attach_task("dc_parent_a1", "dt_parent_cli") == 0;
    }
    if (ok) {
        ok = delegate_task_store_plan("dt_parent_session_ui", "dc_parent_a2", "delegate_sync_parent_session_ui", "explore",
                                      "parent-session-ui", "parent session-ui child", "analyze session-ui module", "deepseek-v4-pro",
                                      session_ui_path, "subsystem", "focus_session_ui", NULL, NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_attach_task("dc_parent_a2", "dt_parent_session_ui") == 0;
    }
    if (ok) {
        ok = delegate_task_store_plan("dt_parent_core", "dc_parent_a2", "delegate_sync_parent_core", "explore",
                                      "parent-core", "parent core child", "analyze core module", "deepseek-v4-pro",
                                      core_path, "subsystem", "focus_core", NULL, NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_attach_task("dc_parent_a2", "dt_parent_core") == 0;
    }
    if (ok) {
        ok = delegate_task_store_plan("dt_parent_sdk", "dc_parent_b1", "delegate_sync_parent_sdk", "explore",
                                      "parent-sdk", "parent sdk child", "analyze sdk module", "deepseek-v4-pro",
                                      sdk_path, "subsystem", "focus_sdk", NULL, NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_attach_task("dc_parent_b1", "dt_parent_sdk") == 0;
    }

    if (ok) {
        ok = delegate_launch_ready_background_subagents_for_runtime() == 0;
    }
    if (ok) {
        ok = wait_for_delegate_lifecycle_snapshot(predicate_delegate_per_parent_cap,
                                                  &wait_state,
                                                  1000);
    }

    if (saved_global_copy[0]) {
        setenv("DELEGATE_BG_MAX_CONCURRENCY", saved_global_copy, 1);
    } else {
        unsetenv("DELEGATE_BG_MAX_CONCURRENCY");
    }
    if (saved_parent_copy[0]) {
        setenv("DELEGATE_BG_MAX_PER_PARENT", saved_parent_copy, 1);
    } else {
        unsetenv("DELEGATE_BG_MAX_PER_PARENT");
    }
    if (saved_delay_copy[0]) {
        setenv("DELEGATE_BG_TEST_TASK_DELAY_MS", saved_delay_copy, 1);
    } else {
        unsetenv("DELEGATE_BG_TEST_TASK_DELAY_MS");
    }

    report("delegate lifecycle runtime respects per-parent cap", ok);
}

static void test_delegate_lifecycle_runtime_ignores_blocked_for_coordinator_cap(void)
{
    delegate_coordinator_record_t snapshot;
    const char *saved_global = getenv("DELEGATE_BG_MAX_CONCURRENCY");
    const char *saved_per = getenv("DELEGATE_BG_MAX_PER_COORDINATOR");
    const char *saved_delay = getenv("DELEGATE_BG_TEST_TASK_DELAY_MS");
    char saved_global_copy[32];
    char saved_per_copy[32];
    char saved_delay_copy[32];
    char blocked_path[PATH_MAX];
    char ready_path[PATH_MAX];
    int ok = 1;

    memset(&snapshot, 0, sizeof(snapshot));
    memset(saved_global_copy, 0, sizeof(saved_global_copy));
    memset(saved_per_copy, 0, sizeof(saved_per_copy));
    memset(saved_delay_copy, 0, sizeof(saved_delay_copy));
    build_workspace_opencode_path(blocked_path, sizeof(blocked_path), "packages/docs");
    build_workspace_opencode_path(ready_path, sizeof(ready_path), "packages/session-ui");
    if (saved_global && saved_global[0]) {
        strscpy(saved_global_copy, saved_global, sizeof(saved_global_copy));
    }
    if (saved_per && saved_per[0]) {
        strscpy(saved_per_copy, saved_per, sizeof(saved_per_copy));
    }
    if (saved_delay && saved_delay[0]) {
        strscpy(saved_delay_copy, saved_delay, sizeof(saved_delay_copy));
    }

    delegate_task_store_reset_for_test();
    setenv("DELEGATE_BG_MAX_CONCURRENCY", "4", 1);
    setenv("DELEGATE_BG_MAX_PER_COORDINATOR", "1", 1);
    setenv("DELEGATE_BG_TEST_TASK_DELAY_MS", "300", 1);

    ok = delegate_task_store_start_coordinator("dc_block_cap", "chat_block_cap", "tr_block_cap", "block-cap", "parallel") == 0;
    if (ok) {
        ok = delegate_task_store_plan("dt_block_cap_docs", "dc_block_cap", "delegate_sync_block_cap_docs", "explore",
                                      "block-cap-docs", "blocked docs child", "analyze docs module", "deepseek-v4-pro",
                                      blocked_path, "subsystem", "focus_docs", NULL, NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_attach_task("dc_block_cap", "dt_block_cap_docs") == 0;
    }
    if (ok) {
        ok = delegate_task_store_claim_queued_task("dt_block_cap_docs");
    }
    if (ok) {
        ok = delegate_task_store_mark_blocked("dt_block_cap_docs",
                                              "permission",
                                              "sudo password required",
                                              "sudo password was not provided") == 0;
    }
    if (ok) {
        ok = delegate_task_store_plan("dt_block_cap_session_ui", "dc_block_cap", "delegate_sync_block_cap_session_ui", "explore",
                                      "block-cap-session-ui", "ready session-ui child", "analyze session-ui module", "deepseek-v4-pro",
                                      ready_path, "subsystem", "focus_session_ui", NULL, NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_attach_task("dc_block_cap", "dt_block_cap_session_ui") == 0;
    }

    if (ok) {
        ok = delegate_launch_ready_background_subagents_for_runtime() == 0;
    }
    if (ok) {
        ok = wait_for_delegate_lifecycle_snapshot(predicate_delegate_blocked_coordinator_cap,
                                                  &snapshot,
                                                  1000);
    }

    if (saved_global_copy[0]) {
        setenv("DELEGATE_BG_MAX_CONCURRENCY", saved_global_copy, 1);
    } else {
        unsetenv("DELEGATE_BG_MAX_CONCURRENCY");
    }
    if (saved_per_copy[0]) {
        setenv("DELEGATE_BG_MAX_PER_COORDINATOR", saved_per_copy, 1);
    } else {
        unsetenv("DELEGATE_BG_MAX_PER_COORDINATOR");
    }
    if (saved_delay_copy[0]) {
        setenv("DELEGATE_BG_TEST_TASK_DELAY_MS", saved_delay_copy, 1);
    } else {
        unsetenv("DELEGATE_BG_TEST_TASK_DELAY_MS");
    }

    report("delegate lifecycle runtime ignores blocked child for coordinator cap", ok);
}

static void test_delegate_lifecycle_runtime_ignores_blocked_for_parent_cap(void)
{
    delegate_coordinator_record_t left;
    delegate_coordinator_record_t right;
    delegate_two_snapshot_wait_t wait_state;
    const char *saved_global = getenv("DELEGATE_BG_MAX_CONCURRENCY");
    const char *saved_parent = getenv("DELEGATE_BG_MAX_PER_PARENT");
    const char *saved_delay = getenv("DELEGATE_BG_TEST_TASK_DELAY_MS");
    char saved_global_copy[32];
    char saved_parent_copy[32];
    char saved_delay_copy[32];
    char blocked_path[PATH_MAX];
    char ready_path[PATH_MAX];
    int ok = 1;

    memset(&left, 0, sizeof(left));
    memset(&right, 0, sizeof(right));
    memset(&wait_state, 0, sizeof(wait_state));
    memset(saved_global_copy, 0, sizeof(saved_global_copy));
    memset(saved_parent_copy, 0, sizeof(saved_parent_copy));
    memset(saved_delay_copy, 0, sizeof(saved_delay_copy));
    wait_state.left = &left;
    wait_state.right = &right;
    build_workspace_opencode_path(blocked_path, sizeof(blocked_path), "packages/docs");
    build_workspace_opencode_path(ready_path, sizeof(ready_path), "packages/core");
    if (saved_global && saved_global[0]) {
        strscpy(saved_global_copy, saved_global, sizeof(saved_global_copy));
    }
    if (saved_parent && saved_parent[0]) {
        strscpy(saved_parent_copy, saved_parent, sizeof(saved_parent_copy));
    }
    if (saved_delay && saved_delay[0]) {
        strscpy(saved_delay_copy, saved_delay, sizeof(saved_delay_copy));
    }

    delegate_task_store_reset_for_test();
    setenv("DELEGATE_BG_MAX_CONCURRENCY", "4", 1);
    setenv("DELEGATE_BG_MAX_PER_PARENT", "1", 1);
    setenv("DELEGATE_BG_TEST_TASK_DELAY_MS", "300", 1);

    ok = delegate_task_store_start_coordinator("dc_parent_blocked", "chat_parent_blocked", "tr_parent_blocked", "parent-blocked", "parallel") == 0;
    if (ok) {
        ok = delegate_task_store_start_coordinator("dc_parent_ready", "chat_parent_blocked", "tr_parent_blocked", "parent-blocked", "parallel") == 0;
    }

    if (ok) {
        ok = delegate_task_store_plan("dt_parent_blocked_docs", "dc_parent_blocked", "delegate_sync_parent_blocked_docs", "explore",
                                      "parent-blocked-docs", "blocked docs child", "analyze docs module", "deepseek-v4-pro",
                                      blocked_path, "subsystem", "focus_docs", NULL, NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_attach_task("dc_parent_blocked", "dt_parent_blocked_docs") == 0;
    }
    if (ok) {
        ok = delegate_task_store_claim_queued_task("dt_parent_blocked_docs");
    }
    if (ok) {
        ok = delegate_task_store_mark_blocked("dt_parent_blocked_docs",
                                              "question",
                                              "Need clarification",
                                              "Need clarification") == 0;
    }
    if (ok) {
        ok = delegate_task_store_plan("dt_parent_ready_core", "dc_parent_ready", "delegate_sync_parent_ready_core", "explore",
                                      "parent-ready-core", "ready core child", "analyze core module", "deepseek-v4-pro",
                                      ready_path, "subsystem", "focus_core", NULL, NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_attach_task("dc_parent_ready", "dt_parent_ready_core") == 0;
    }

    if (ok) {
        ok = delegate_launch_ready_background_subagents_for_runtime() == 0;
    }
    if (ok) {
        ok = wait_for_delegate_lifecycle_snapshot(predicate_delegate_blocked_parent_cap,
                                                  &wait_state,
                                                  1000);
    }

    if (saved_global_copy[0]) {
        setenv("DELEGATE_BG_MAX_CONCURRENCY", saved_global_copy, 1);
    } else {
        unsetenv("DELEGATE_BG_MAX_CONCURRENCY");
    }
    if (saved_parent_copy[0]) {
        setenv("DELEGATE_BG_MAX_PER_PARENT", saved_parent_copy, 1);
    } else {
        unsetenv("DELEGATE_BG_MAX_PER_PARENT");
    }
    if (saved_delay_copy[0]) {
        setenv("DELEGATE_BG_TEST_TASK_DELAY_MS", saved_delay_copy, 1);
    } else {
        unsetenv("DELEGATE_BG_TEST_TASK_DELAY_MS");
    }

    report("delegate lifecycle runtime ignores blocked child for parent cap", ok);
}

static void test_delegate_background_launch_does_not_finish_with_queued_children(void)
{
    delegate_coordinator_record_t snapshot;
    int ok = 1;
    char app_path[PATH_MAX];
    char cli_path[PATH_MAX];
    char session_ui_path[PATH_MAX];

    memset(&snapshot, 0, sizeof(snapshot));
    delegate_task_store_reset_for_test();
    build_workspace_opencode_path(app_path, sizeof(app_path), "packages/app");
    build_workspace_opencode_path(cli_path, sizeof(cli_path), "packages/cli");
    build_workspace_opencode_path(session_ui_path, sizeof(session_ui_path), "packages/session-ui");

    ok = delegate_task_store_start_coordinator("dc_launch_consistency",
                                               "chat_launch_consistency",
                                               "tr_launch_consistency",
                                               "launch-consistency-team",
                                               "parallel") == 0;
    if (ok) {
        ok = delegate_task_store_plan("dt_launch_app",
                                      "dc_launch_consistency",
                                      "delegate_sync_launch_app",
                                      "explore",
                                      "app",
                                      "analyze app",
                                      "prompt app",
                                      "deepseek-v4-pro",
                                      app_path,
                                      "subsystem",
                                      "local_overview",
                                      NULL,
                                      NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_attach_task("dc_launch_consistency", "dt_launch_app") == 0;
    }
    if (ok) {
        ok = delegate_task_store_plan("dt_launch_cli",
                                      "dc_launch_consistency",
                                      "delegate_sync_launch_cli",
                                      "explore",
                                      "cli",
                                      "analyze cli",
                                      "prompt cli",
                                      "deepseek-v4-pro",
                                      cli_path,
                                      "subsystem",
                                      "local_overview",
                                      NULL,
                                      NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_attach_task("dc_launch_consistency", "dt_launch_cli") == 0;
    }
    if (ok) {
        ok = delegate_task_store_plan("dt_launch_session_ui",
                                      "dc_launch_consistency",
                                      "delegate_sync_launch_session_ui",
                                      "explore",
                                      "session-ui",
                                      "analyze session-ui",
                                      "prompt session-ui",
                                      "deepseek-v4-pro",
                                      session_ui_path,
                                      "subsystem",
                                      "local_overview",
                                      NULL,
                                      NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_attach_task("dc_launch_consistency", "dt_launch_session_ui") == 0;
    }
    if (ok) {
        ok = tool_delegate_launch_ready_background_subagents("dc_launch_consistency",
                                                             "chat_launch_consistency") == 0;
    }
    if (ok) {
        usleep(200000);
        ok = delegate_task_store_snapshot_coordinator("dc_launch_consistency", &snapshot) == 0;
    }
    if (ok) {
        bool has_queued = false;
        bool inconsistent_done = false;

        for (int i = 0; i < snapshot.agent_count; i++) {
            if (strcmp(snapshot.agents[i].status, "queued") == 0) {
                has_queued = true;
            }
        }
        inconsistent_done = strcmp(snapshot.status, "done") == 0 && has_queued;
        if (inconsistent_done) {
            pr_warn("  launch consistency diag: coordinator status=%s queued_count=%d running_count=%d completed_count=%d",
                    snapshot.status,
                    snapshot.queued_count,
                    snapshot.running_count,
                    snapshot.completed_count);
            for (int i = 0; i < snapshot.agent_count; i++) {
                pr_warn("  launch consistency agent[%d]: task_id=%s status=%s scope=%s",
                        i,
                        snapshot.agents[i].task_id,
                        snapshot.agents[i].status,
                        snapshot.agents[i].scope_path);
            }
        }
        ok = !inconsistent_done;
    }

    report("delegate background launch does not finish with queued children", ok);
}

static void test_delegate_background_launch_long_prompts_keep_all_children_schedulable(void)
{
    delegate_request_t req;
    char output[4096];
    delegate_coordinator_record_t snapshot;
    delegate_single_snapshot_wait_t wait_state;
    int ok = 1;
    char long_prompt[2000];
    char app_path[PATH_MAX];
    char cli_path[PATH_MAX];
    char session_ui_path[PATH_MAX];

    memset(&req, 0, sizeof(req));
    memset(output, 0, sizeof(output));
    memset(&snapshot, 0, sizeof(snapshot));
    memset(&wait_state, 0, sizeof(wait_state));
    memset(long_prompt, 0, sizeof(long_prompt));
    build_workspace_opencode_path(app_path, sizeof(app_path), "packages/app");
    build_workspace_opencode_path(cli_path, sizeof(cli_path), "packages/cli");
    build_workspace_opencode_path(session_ui_path, sizeof(session_ui_path), "packages/session-ui");

    for (size_t i = 0; i + 2 < sizeof(long_prompt); i += 2) {
        long_prompt[i] = 'A' + (char)((i / 2) % 26);
        long_prompt[i + 1] = ' ';
    }
    long_prompt[1800] = '\0';

    delegate_task_store_reset_for_test();

    req.is_batch = true;
    req.batch_count = 3;
    strscpy(req.team_name, "delegate-team", sizeof(req.team_name));
    strscpy(req.dispatch_mode, "parallel", sizeof(req.dispatch_mode));

    strscpy(req.batch_tasks[0].task_key, "packagesapp", sizeof(req.batch_tasks[0].task_key));
    strscpy(req.batch_tasks[0].description, "分析 packages/app 模块", sizeof(req.batch_tasks[0].description));
    strscpy(req.batch_tasks[0].subagent_type, "explore", sizeof(req.batch_tasks[0].subagent_type));
    strscpy(req.batch_tasks[0].target_path, app_path, sizeof(req.batch_tasks[0].target_path));
    snprintf(req.batch_tasks[0].prompt, sizeof(req.batch_tasks[0].prompt),
             "分析 opencode/packages/app 的目录结构、关键模块、入口文件、主要功能。\n\n%s",
             long_prompt);

    strscpy(req.batch_tasks[1].task_key, "packagescli", sizeof(req.batch_tasks[1].task_key));
    strscpy(req.batch_tasks[1].description, "分析 packages/cli 模块", sizeof(req.batch_tasks[1].description));
    strscpy(req.batch_tasks[1].subagent_type, "explore", sizeof(req.batch_tasks[1].subagent_type));
    strscpy(req.batch_tasks[1].target_path, cli_path, sizeof(req.batch_tasks[1].target_path));
    snprintf(req.batch_tasks[1].prompt, sizeof(req.batch_tasks[1].prompt),
             "分析 opencode/packages/cli 的目录结构、关键模块、入口文件、主要功能。\n\n%s",
             long_prompt);

    strscpy(req.batch_tasks[2].task_key, "packagessession-ui", sizeof(req.batch_tasks[2].task_key));
    strscpy(req.batch_tasks[2].description, "分析 packages/session-ui 模块", sizeof(req.batch_tasks[2].description));
    strscpy(req.batch_tasks[2].subagent_type, "explore", sizeof(req.batch_tasks[2].subagent_type));
    strscpy(req.batch_tasks[2].target_path, session_ui_path, sizeof(req.batch_tasks[2].target_path));
    snprintf(req.batch_tasks[2].prompt, sizeof(req.batch_tasks[2].prompt),
             "分析 opencode/packages/session-ui 的目录结构、关键模块、入口文件、主要功能。\n\n%s",
             long_prompt);

    if (ok) {
        ok = tool_delegate_run_background_coordinator(&req,
                                                      "chat_long_prompt_batch",
                                                      output,
                                                      sizeof(output)) == 0;
    }
    if (ok) {
        char coordinator_id[DELEGATE_COORDINATOR_ID_LEN];
        coordinator_id[0] = '\0';
        cJSON *root = cJSON_Parse(output);
        cJSON *coord = root ? cJSON_GetObjectItem(root, "coordinator_id") : NULL;
        const char *coord_id = cJSON_GetStringValue(coord);
        ok = coord_id && coord_id[0];
        if (ok) {
            strscpy(coordinator_id, coord_id, sizeof(coordinator_id));
            strscpy(wait_state.coordinator_id, coord_id, sizeof(wait_state.coordinator_id));
            wait_state.snapshot = &snapshot;
        }
        cJSON_Delete(root);
        if (ok) {
            ok = wait_for_delegate_lifecycle_snapshot(predicate_delegate_long_prompt_schedulable,
                                                      &wait_state,
                                                      1000);
        }
    }
    if (ok) {
        int queued = 0;
        int running = 0;
        int done = 0;
        for (int i = 0; i < snapshot.agent_count; i++) {
            if (strcmp(snapshot.agents[i].status, "queued") == 0) {
                queued++;
            } else if (strcmp(snapshot.agents[i].status, "running") == 0) {
                running++;
            } else if (strcmp(snapshot.agents[i].status, "done") == 0) {
                done++;
            }
        }
        if (queued > 0 && strcmp(snapshot.status, "done") == 0) {
            pr_warn("  long prompt launch diag: status=%s queued=%d running=%d done=%d",
                    snapshot.status, queued, running, done);
            for (int i = 0; i < snapshot.agent_count; i++) {
                pr_warn("  long prompt launch agent[%d]: task_id=%s status=%s desc=%s",
                        i,
                        snapshot.agents[i].task_id,
                        snapshot.agents[i].status,
                        snapshot.agents[i].description);
            }
        }
        ok = !(queued > 0 && strcmp(snapshot.status, "done") == 0);
    }

    report("delegate background launch long prompts keep all children schedulable", ok);
}

static void test_delegate_task_store_requires_effective_output_for_done(void)
{
    delegate_task_store_reset_for_test();
    delegate_coordinator_record_t snapshot;
    delegate_task_record_t task_snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    memset(&task_snapshot, 0, sizeof(task_snapshot));

    int ok = delegate_task_store_start_coordinator("dc_empty", "chat_empty", "tr_empty", "empty-team", "parallel") == 0;
    if (ok) {
        ok = delegate_task_store_start("dt_empty", "dc_empty", "delegate_sync_empty", "explore",
                                       "empty-output", "empty output", "prompt", "deepseek-v4-pro",
                                       "kernel", "subsystem", "execution_kernel", NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_attach_task("dc_empty", "dt_empty") == 0;
    }
    if (ok) {
        ok = delegate_task_store_mark_blocked("dt_empty",
                                              "permission",
                                              "sudo password was not provided",
                                              "{\"command\":\"sudo ls /root\",\"workdir\":\"/home/wangergou/code/github/daima-agent\",\"exit_code\":1,\"timed_out\":false,\"truncated\":false,\"output\":\"sudo password was not provided\",\"error\":\"sudo_password_cancelled\"}") == 0;
    }
    if (ok) {
        ok = delegate_task_store_complete("dt_empty",
                                          "{\"command\":\"sudo ls /root\",\"workdir\":\"/home/wangergou/code/github/daima-agent\",\"exit_code\":1,\"timed_out\":false,\"truncated\":false,\"output\":\"sudo password was not provided\",\"error\":\"sudo_password_cancelled\"}",
                                          "",
                                          false) == 0;
    }

    if (delegate_task_store_snapshot_coordinator("dc_empty", &snapshot) != 0) {
        ok = 0;
    }
    if (ok && delegate_task_store_snapshot("dt_empty", &task_snapshot) != 0) {
        ok = 0;
    }
    if (ok) {
        ok = strcmp(snapshot.status, "failed") == 0 &&
             snapshot.blocked_count == 1 &&
             snapshot.failed_count == 1 &&
             snapshot.effective_output_count == 0 &&
             strcmp(snapshot.blocker_kind, "permission") == 0 &&
             strstr(snapshot.blocker_text, "sudo password") != NULL &&
             task_snapshot.status == DELEGATE_TASK_FAILED &&
             strcmp(task_snapshot.blocker_kind, "permission") == 0;
    }
    if (!ok) {
        pr_info("  empty_output diag: status=%s blocked_count=%d failed_count=%d effective_output_count=%d blocker_kind=%s blocker_text=%s task_status=%d completed=%d failed=%d",
                snapshot.status,
                snapshot.blocked_count,
                snapshot.failed_count,
                snapshot.effective_output_count,
                snapshot.blocker_kind,
                snapshot.blocker_text,
                task_snapshot.status,
                snapshot.completed_count,
                snapshot.failed_count);
    }

    report("delegate task store requires effective output for done", ok);
}

static void test_delegate_task_store_plan_fails_when_store_is_full(void)
{
    delegate_task_store_reset_for_test();
    delegate_task_record_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));

    int ok = 1;
    for (int i = 0; i < DELEGATE_TASK_STORE_MAX; i++) {
        char task_id[32];
        char session_id[32];
        snprintf(task_id, sizeof(task_id), "dt_full_%d", i);
        snprintf(session_id, sizeof(session_id), "delegate_full_%d", i);
        ok = ok && delegate_task_store_plan(task_id,
                                            "dc_full",
                                            session_id,
                                            "explore",
                                            "",
                                            "fill store",
                                            "fill prompt",
                                            "model",
                                            "/tmp/fill",
                                            "repo_root",
                                            "structure",
                                            "",
                                            NULL) == 0;
    }

    if (ok) {
        ok = delegate_task_store_plan("dt_full_overflow",
                                      "dc_full",
                                      "delegate_full_overflow",
                                      "explore",
                                      "",
                                      "overflow",
                                      "overflow prompt",
                                      "model",
                                      "/tmp/overflow",
                                      "repo_root",
                                      "structure",
                                      "",
                                      NULL) == ERR_NO_MEM;
    }
    if (ok) {
        ok = delegate_task_store_snapshot("dt_full_0", &snapshot) == 0 &&
             strcmp(snapshot.description, "fill store") == 0;
    }
    if (ok) {
        ok = delegate_task_store_snapshot("dt_full_overflow", &snapshot) == ERR_NOT_FOUND;
    }

    report("delegate task store plan fails when store is full", ok);
}

static void test_delegate_task_store_retains_child_session_history(void)
{
    delegate_task_store_reset_for_test();
    delegate_task_record_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));

    int ok = delegate_task_store_start("dt_hist",
                                       "dc_hist",
                                       "delegate_sync_hist",
                                       "explore",
                                       "map-hist",
                                       "history task",
                                       "prompt",
                                       "deepseek-v4-pro",
                                       "kernel/tooling",
                                       "subsystem",
                                       "coordination",
                                       NULL) == 0;
    if (ok) {
        ok = delegate_task_store_mark_blocked("dt_hist",
                                              "permission",
                                              "Need sudo approval",
                                              "sudo password was not provided") == 0;
    }
    if (ok) {
        ok = delegate_task_store_clear_blocked("dt_hist") == 0;
    }
    if (ok) {
        ok = delegate_task_store_complete("dt_hist", "history final summary", "", false) == 0;
    }
    if (ok) {
        ok = delegate_task_store_snapshot("dt_hist", &snapshot) == 0 &&
             strstr(snapshot.child_session.summary, "history final summary") != NULL &&
             snapshot.child_session.frame_count >= 4 &&
             snapshot.child_session.commit_count >= 4 &&
             strcmp(snapshot.child_session.frames[1].type, "subagent_blocked") == 0 &&
             strcmp(snapshot.child_session.frames[2].type, "subagent_unblocked") == 0 &&
             strcmp(snapshot.child_session.frames[snapshot.child_session.frame_count - 1].type, "subagent_done") == 0 &&
             strcmp(snapshot.child_session.commits[snapshot.child_session.commit_count - 1].kind, "result") == 0;
    }
    if (!ok) {
        pr_info("  child_session diag: summary=%s frame_count=%d commit_count=%d last_frame=%s last_commit=%s",
                snapshot.child_session.summary,
                snapshot.child_session.frame_count,
                snapshot.child_session.commit_count,
                snapshot.child_session.frame_count > 0
                    ? snapshot.child_session.frames[snapshot.child_session.frame_count - 1].type
                    : "",
                snapshot.child_session.commit_count > 0
                    ? snapshot.child_session.commits[snapshot.child_session.commit_count - 1].kind
                    : "");
    }

    report("delegate task store retains child session history", ok);
}

static void test_delegate_task_store_plan_retains_preflight_tool(void)
{
    delegate_task_store_reset_for_test();
    delegate_coordinator_record_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));

    delegate_preflight_tool_view_t preflight = {0};
    strscpy(preflight.tool_name, "terminal", sizeof(preflight.tool_name));
    strscpy(preflight.input_json,
            "{\"command\":\"sudo ls /root\",\"workdir\":\"/home/wangergou/code/github/daima-agent\"}",
            sizeof(preflight.input_json));
    preflight.continue_on_error = false;

    int ok = delegate_task_store_start_coordinator("dc_preflight", "chat_preflight", "tr_preflight", "preflight-team", "parallel") == 0;
    if (ok) {
        ok = delegate_task_store_plan("dt_preflight", "dc_preflight", "delegate_sync_preflight", "explore",
                                      "sudo-check", "sudo check", "prompt", "deepseek-v4-pro",
                                      "/home/wangergou/code/github/daima-agent", "subsystem", "tool_runtime", "",
                                      &preflight) == 0;
    }
    if (ok) {
        ok = delegate_task_store_attach_task("dc_preflight", "dt_preflight") == 0;
    }
    if (ok) {
        ok = delegate_task_store_snapshot_coordinator("dc_preflight", &snapshot) == 0 &&
             snapshot.agent_count == 1 &&
             strcmp(snapshot.agents[0].preflight_tool.tool_name, "terminal") == 0 &&
             strstr(snapshot.agents[0].preflight_tool.input_json, "sudo ls /root") &&
             !snapshot.agents[0].preflight_tool.continue_on_error;
    }

    report("delegate task store plan retains preflight tool", ok);
}

static void test_delegate_task_store_records_child_session_step_event(void)
{
    delegate_task_store_reset_for_test();
    delegate_task_record_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));

    int ok = delegate_task_store_start("dt_step",
                                       "dc_step",
                                       "delegate_sync_step",
                                       "explore",
                                       "step-task",
                                       "step task",
                                       "prompt",
                                       "deepseek-v4-pro",
                                       "/home/wangergou/code/github/daima-agent",
                                       "subsystem",
                                       "tool_runtime",
                                       NULL) == 0;
    if (ok) {
        ok = delegate_task_store_append_session_step("dt_step",
                                                     "tool",
                                                     "preflight terminal sudo check",
                                                     "sudo ls /root => sudo password was not provided") == 0;
    }
    if (ok) {
        ok = delegate_task_store_snapshot("dt_step", &snapshot) == 0 &&
             snapshot.child_session.frame_count >= 2 &&
             strcmp(snapshot.child_session.frames[snapshot.child_session.frame_count - 1].type, "subagent_step") == 0 &&
             strcmp(snapshot.child_session.frames[snapshot.child_session.frame_count - 1].phase, "progress") == 0 &&
             strcmp(snapshot.child_session.frames[snapshot.child_session.frame_count - 1].blocker_kind, "tool") == 0 &&
             strstr(snapshot.child_session.frames[snapshot.child_session.frame_count - 1].detail, "preflight terminal sudo check") != NULL &&
             snapshot.child_session.commit_count >= 2 &&
             strcmp(snapshot.child_session.commits[snapshot.child_session.commit_count - 1].kind, "tool") == 0 &&
             strcmp(snapshot.child_session.commits[snapshot.child_session.commit_count - 1].phase, "running") == 0 &&
             strstr(snapshot.child_session.commits[snapshot.child_session.commit_count - 1].text, "preflight terminal sudo check") != NULL;
    }
    if (!ok) {
        pr_info("  session_step diag: frame_count=%d commit_count=%d last_frame=%s last_phase=%s last_kind=%s last_commit=%s",
                snapshot.child_session.frame_count,
                snapshot.child_session.commit_count,
                snapshot.child_session.frame_count > 0
                    ? snapshot.child_session.frames[snapshot.child_session.frame_count - 1].type
                    : "",
                snapshot.child_session.frame_count > 0
                    ? snapshot.child_session.frames[snapshot.child_session.frame_count - 1].phase
                    : "",
                snapshot.child_session.frame_count > 0
                    ? snapshot.child_session.frames[snapshot.child_session.frame_count - 1].blocker_kind
                    : "",
                snapshot.child_session.commit_count > 0
                    ? snapshot.child_session.commits[snapshot.child_session.commit_count - 1].kind
                    : "");
    }

    report("delegate task store records child session step event", ok);
}

static void test_delegate_task_store_step_event_renders_result_json_visible_text(void)
{
    delegate_task_store_reset_for_test();
    delegate_task_record_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));

    int ok = delegate_task_store_start("dt_step_json",
                                       "dc_step_json",
                                       "delegate_sync_step_json",
                                       "oracle",
                                       "step-json-task",
                                       "step json task",
                                       "prompt",
                                       "deepseek-v4-pro",
                                       "/home/wangergou/code/github/daima-agent",
                                       "subsystem",
                                       "delegate",
                                       NULL) == 0;
    if (ok) {
        ok = delegate_task_store_append_session_step(
                 "dt_step_json",
                 "tool",
                 "dependency merge shortcut",
                 "{\"status\":\"done\",\"summary\":\"Readable step summary\",\"evidence\":[\"kernel/tooling/delegate/delegate_task_projection.c\"]}") == 0;
    }
    if (ok) {
        ok = delegate_task_store_snapshot("dt_step_json", &snapshot) == 0 &&
             snapshot.child_session.frame_count >= 2 &&
             strcmp(snapshot.child_session.frames[snapshot.child_session.frame_count - 1].type, "subagent_step") == 0 &&
             strcmp(snapshot.child_session.frames[snapshot.child_session.frame_count - 1].blocker_kind, "tool") == 0 &&
             strcmp(snapshot.child_session.frames[snapshot.child_session.frame_count - 1].output_preview,
                    "Readable step summary\n\nEvidence:\n- kernel/tooling/delegate/delegate_task_projection.c") == 0;
    }
    if (!ok) {
        pr_info("  session_step_json diag: frame_count=%d output_preview=%s",
                snapshot.child_session.frame_count,
                snapshot.child_session.frame_count > 0
                    ? snapshot.child_session.frames[snapshot.child_session.frame_count - 1].output_preview
                    : "");
    }

    report("delegate task store step event renders result json visible text", ok);
}

static void test_delegate_runtime_tool_call_records_child_session_step(void)
{
    delegate_task_store_reset_for_test();
    delegate_task_record_t snapshot;
    struct message msg;
    llm_tool_call_t call;
    tool_runtime_result_t rt;
    char output[4096];
    memset(&snapshot, 0, sizeof(snapshot));
    memset(&msg, 0, sizeof(msg));
    memset(&call, 0, sizeof(call));
    memset(&rt, 0, sizeof(rt));
    memset(output, 0, sizeof(output));

    int ok = delegate_task_store_start("dt_runtime_step",
                                       "dc_runtime_step",
                                       "delegate_sync_runtime_step",
                                       "explore",
                                       "runtime-step-task",
                                       "runtime step task",
                                       "prompt",
                                       "deepseek-v4-pro",
                                       "/home/wangergou/code/github/daima-agent",
                                       "subsystem",
                                       "tool_runtime",
                                       NULL) == 0;
    if (ok) {
        strscpy(msg.channel, "system", sizeof(msg.channel));
        strscpy(msg.chat_id, "delegate_sync_runtime_step", sizeof(msg.chat_id));
        strscpy(msg.source, "internal", sizeof(msg.source));
        msg.content = strdup("runtime step");
        ok = msg.content != NULL;
    }
    if (ok) {
        strscpy(call.id, "runtime_step_tool", sizeof(call.id));
        strscpy(call.name, "get_current_time", sizeof(call.name));
        call.input = "{}";
        call.input_len = strlen(call.input);
        ok = tool_runtime_execute_call(&call, &msg, output, sizeof(output), &rt) == 0;
    }
    if (ok) {
        ok = delegate_task_store_snapshot("dt_runtime_step", &snapshot) == 0 &&
             snapshot.child_session.frame_count >= 2 &&
             strcmp(snapshot.child_session.frames[snapshot.child_session.frame_count - 1].type, "subagent_step") == 0 &&
             strcmp(snapshot.child_session.frames[snapshot.child_session.frame_count - 1].blocker_kind, "tool") == 0 &&
             strstr(snapshot.child_session.frames[snapshot.child_session.frame_count - 1].detail, "get_current_time") != NULL &&
             snapshot.child_session.commits[snapshot.child_session.commit_count - 1].kind[0] &&
             strcmp(snapshot.child_session.commits[snapshot.child_session.commit_count - 1].kind, "tool") == 0 &&
             strstr(snapshot.child_session.commits[snapshot.child_session.commit_count - 1].text, "get_current_time") != NULL;
    }
    if (!ok) {
        pr_info("  runtime_step diag: output=%s frame_count=%d commit_count=%d last_frame=%s last_kind=%s last_detail=%s last_commit=%s",
                output,
                snapshot.child_session.frame_count,
                snapshot.child_session.commit_count,
                snapshot.child_session.frame_count > 0
                    ? snapshot.child_session.frames[snapshot.child_session.frame_count - 1].type
                    : "",
                snapshot.child_session.frame_count > 0
                    ? snapshot.child_session.frames[snapshot.child_session.frame_count - 1].blocker_kind
                    : "",
                snapshot.child_session.frame_count > 0
                    ? snapshot.child_session.frames[snapshot.child_session.frame_count - 1].detail
                    : "",
                snapshot.child_session.commit_count > 0
                    ? snapshot.child_session.commits[snapshot.child_session.commit_count - 1].kind
                    : "");
    }

    free(msg.content);
    report("delegate runtime tool call records child session step", ok);
}

static void test_delegate_task_store_step_history_precedes_done(void)
{
    delegate_task_store_reset_for_test();
    delegate_task_record_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));

    int ok = delegate_task_store_start("dt_step_done",
                                       "dc_step_done",
                                       "delegate_sync_step_done",
                                       "explore",
                                       "step-done-task",
                                       "step done task",
                                       "prompt",
                                       "deepseek-v4-pro",
                                       "/home/wangergou/code/github/daima-agent/kernel/turn",
                                       "subsystem",
                                       "turn_execution",
                                       NULL) == 0;
    if (ok) {
        ok = delegate_task_store_append_session_step("dt_step_done",
                                                     "tool",
                                                     "local repo overview shortcut",
                                                     "turn 目录概览已生成") == 0;
    }
    if (ok) {
        ok = delegate_task_store_complete("dt_step_done", "turn 目录概览已生成", "", false) == 0;
    }
    if (ok) {
        ok = delegate_task_store_snapshot("dt_step_done", &snapshot) == 0 &&
             snapshot.child_session.frame_count >= 3 &&
             strcmp(snapshot.child_session.frames[snapshot.child_session.frame_count - 2].type, "subagent_step") == 0 &&
             strcmp(snapshot.child_session.frames[snapshot.child_session.frame_count - 1].type, "subagent_done") == 0 &&
             snapshot.child_session.commit_count >= 3 &&
             strcmp(snapshot.child_session.commits[snapshot.child_session.commit_count - 2].kind, "tool") == 0 &&
             strcmp(snapshot.child_session.commits[snapshot.child_session.commit_count - 1].kind, "result") == 0;
    }
    if (!ok) {
        pr_info("  session_step_done diag: frame_count=%d commit_count=%d prev_frame=%s last_frame=%s prev_commit=%s last_commit=%s",
                snapshot.child_session.frame_count,
                snapshot.child_session.commit_count,
                snapshot.child_session.frame_count >= 2
                    ? snapshot.child_session.frames[snapshot.child_session.frame_count - 2].type
                    : "",
                snapshot.child_session.frame_count >= 1
                    ? snapshot.child_session.frames[snapshot.child_session.frame_count - 1].type
                    : "",
                snapshot.child_session.commit_count >= 2
                    ? snapshot.child_session.commits[snapshot.child_session.commit_count - 2].kind
                    : "",
                snapshot.child_session.commit_count >= 1
                    ? snapshot.child_session.commits[snapshot.child_session.commit_count - 1].kind
                    : "");
    }

    report("delegate task store step history precedes done", ok);
}

static void test_delegate_task_store_records_child_session_message_commits(void)
{
    delegate_task_store_reset_for_test();
    delegate_task_record_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));

    int ok = delegate_task_store_start("dt_msg_commit",
                                       "dc_msg_commit",
                                       "delegate_sync_msg_commit",
                                       "explore",
                                       "msg-commit-task",
                                       "message commit task",
                                       "prompt",
                                       "deepseek-v4-pro",
                                       "/home/wangergou/code/github/daima-agent",
                                       "subsystem",
                                       "tool_runtime",
                                       NULL) == 0;
    if (ok) {
        ok = delegate_task_store_append_session_message("dt_msg_commit",
                                                        "assistant",
                                                        "分析结论：kernel/turn 负责 turn 执行主链") == 0;
    }
    if (ok) {
        ok = delegate_task_store_append_session_message("dt_msg_commit",
                                                        "reasoning",
                                                        "先检查 turn_run 和 turn_exec 的边界，再确认 resume 路径") == 0;
    }
    if (ok) {
        ok = delegate_task_store_snapshot("dt_msg_commit", &snapshot) == 0 &&
             snapshot.child_session.frame_count >= 3 &&
             strcmp(snapshot.child_session.frames[snapshot.child_session.frame_count - 2].type, "subagent_message") == 0 &&
             strcmp(snapshot.child_session.frames[snapshot.child_session.frame_count - 2].blocker_kind, "assistant") == 0 &&
             strstr(snapshot.child_session.frames[snapshot.child_session.frame_count - 2].detail, "kernel/turn") != NULL &&
             strcmp(snapshot.child_session.frames[snapshot.child_session.frame_count - 1].type, "subagent_message") == 0 &&
             strcmp(snapshot.child_session.frames[snapshot.child_session.frame_count - 1].blocker_kind, "reasoning") == 0 &&
             strstr(snapshot.child_session.frames[snapshot.child_session.frame_count - 1].detail, "turn_run") != NULL &&
             snapshot.child_session.commit_count >= 3 &&
             strcmp(snapshot.child_session.commits[snapshot.child_session.commit_count - 2].kind, "assistant") == 0 &&
             strcmp(snapshot.child_session.commits[snapshot.child_session.commit_count - 1].kind, "reasoning") == 0;
    }
    if (!ok) {
        pr_info("  session_message diag: frame_count=%d commit_count=%d prev_frame=%s prev_kind=%s last_frame=%s last_kind=%s",
                snapshot.child_session.frame_count,
                snapshot.child_session.commit_count,
                snapshot.child_session.frame_count >= 2
                    ? snapshot.child_session.frames[snapshot.child_session.frame_count - 2].type
                    : "",
                snapshot.child_session.frame_count >= 2
                    ? snapshot.child_session.frames[snapshot.child_session.frame_count - 2].blocker_kind
                    : "",
                snapshot.child_session.frame_count >= 1
                    ? snapshot.child_session.frames[snapshot.child_session.frame_count - 1].type
                    : "",
                snapshot.child_session.commit_count >= 1
                    ? snapshot.child_session.commits[snapshot.child_session.commit_count - 1].kind
                    : "");
    }

    report("delegate task store records child session message commits", ok);
}

static void test_delegate_task_store_retains_richer_child_session_history_window(void)
{
    delegate_task_store_reset_for_test();
    delegate_task_record_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));

    int ok = delegate_task_store_start("dt_hist_window",
                                       "dc_hist_window",
                                       "delegate_sync_hist_window",
                                       "explore",
                                       "hist-window-task",
                                       "history window task",
                                       "prompt",
                                       "deepseek-v4-pro",
                                       "/home/wangergou/code/github/daima-agent/kernel",
                                       "subsystem",
                                       "execution_kernel",
                                       NULL) == 0;
    if (ok) {
        for (int i = 0; i < 12; i++) {
            char detail[96];
            snprintf(detail, sizeof(detail), "step-%02d", i);
            if (delegate_task_store_append_session_step("dt_hist_window",
                                                        "tool",
                                                        detail,
                                                        detail) != 0) {
                ok = 0;
                break;
            }
        }
    }
    if (ok) {
        ok = delegate_task_store_complete("dt_hist_window", "history window final summary", "", false) == 0;
    }
    if (ok) {
        ok = delegate_task_store_snapshot("dt_hist_window", &snapshot) == 0 &&
             snapshot.child_session.frame_count >= 12 &&
             snapshot.child_session.commit_count >= 12 &&
             strcmp(snapshot.child_session.frames[1].type, "subagent_step") == 0 &&
             strstr(snapshot.child_session.frames[1].detail, "step-00") != NULL &&
             strcmp(snapshot.child_session.frames[snapshot.child_session.frame_count - 1].type, "subagent_done") == 0 &&
             strcmp(snapshot.child_session.commits[1].kind, "tool") == 0 &&
             strstr(snapshot.child_session.commits[1].text, "step-00") != NULL &&
             strcmp(snapshot.child_session.commits[snapshot.child_session.commit_count - 1].kind, "result") == 0;
    }
    if (!ok) {
        pr_info("  hist_window diag: frame_count=%d commit_count=%d frame1=%s detail1=%s commit1=%s text1=%s last_frame=%s last_commit=%s",
                snapshot.child_session.frame_count,
                snapshot.child_session.commit_count,
                snapshot.child_session.frame_count > 1
                    ? snapshot.child_session.frames[1].type
                    : "",
                snapshot.child_session.frame_count > 1
                    ? snapshot.child_session.frames[1].detail
                    : "",
                snapshot.child_session.commit_count > 1
                    ? snapshot.child_session.commits[1].kind
                    : "",
                snapshot.child_session.commit_count > 1
                    ? snapshot.child_session.commits[1].text
                    : "",
                snapshot.child_session.frame_count > 0
                    ? snapshot.child_session.frames[snapshot.child_session.frame_count - 1].type
                    : "",
                snapshot.child_session.commit_count > 0
                    ? snapshot.child_session.commits[snapshot.child_session.commit_count - 1].kind
                    : "");
    }

    report("delegate task store retains richer child session history window", ok);
}

static void test_delegate_task_store_session_seq_stays_monotonic_across_trim(void)
{
    delegate_task_store_reset_for_test();

    delegate_task_record_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));

    int ok = delegate_task_store_start("dt_trim_seq",
                                       "dc_trim_seq",
                                       "delegate_sync_trim_seq",
                                       "explore",
                                       "trim-seq-task",
                                       "trim seq task",
                                       "prompt",
                                       "deepseek-v4-pro",
                                       "/home/wangergou/code/github/daima-agent/kernel",
                                       "subsystem",
                                       "execution_kernel",
                                       NULL) == 0;
    if (ok) {
        for (int i = 0; i < 96; i++) {
            char detail[96];
            snprintf(detail, sizeof(detail), "seq-step-%02d", i);
            if (delegate_task_store_append_session_step("dt_trim_seq", "tool", detail, detail) != 0) {
                ok = 0;
                break;
            }
        }
    }
    if (ok) {
        ok = delegate_task_store_complete("dt_trim_seq", "trim seq summary", "", false) == 0;
    }
    if (ok) {
        ok = delegate_task_store_snapshot("dt_trim_seq", &snapshot) == 0 &&
             snapshot.child_session.frame_count == DELEGATE_SESSION_FRAME_LIMIT &&
             snapshot.child_session.commit_count == DELEGATE_SESSION_COMMIT_LIMIT &&
             snapshot.child_session.frames[0].seq > 1 &&
             snapshot.child_session.commits[0].seq > 1 &&
             snapshot.child_session.frames[0].seq < snapshot.child_session.frames[snapshot.child_session.frame_count - 1].seq &&
             snapshot.child_session.commits[0].seq < snapshot.child_session.commits[snapshot.child_session.commit_count - 1].seq;
    }
    if (!ok) {
        pr_info("  trim_seq diag: frame_count=%d commit_count=%d first_frame_seq=%lu last_frame_seq=%lu first_commit_seq=%lu last_commit_seq=%lu next_frame_seq=%lu next_commit_seq=%lu",
                snapshot.child_session.frame_count,
                snapshot.child_session.commit_count,
                snapshot.child_session.frame_count > 0 ? snapshot.child_session.frames[0].seq : 0,
                snapshot.child_session.frame_count > 0 ? snapshot.child_session.frames[snapshot.child_session.frame_count - 1].seq : 0,
                snapshot.child_session.commit_count > 0 ? snapshot.child_session.commits[0].seq : 0,
                snapshot.child_session.commit_count > 0 ? snapshot.child_session.commits[snapshot.child_session.commit_count - 1].seq : 0,
                snapshot.child_session.frame_seq_next,
                snapshot.child_session.commit_seq_next);
    }

    report("delegate task store session seq stays monotonic across trim", ok);
}

static void test_delegate_child_session_json_retains_recent_history_window(void)
{
    delegate_task_store_reset_for_test();
    session_store_clear("delegate_sync_hist_json");

    delegate_task_record_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));

    int ok = delegate_task_store_start("dt_hist_json",
                                       "dc_hist_json",
                                       "delegate_sync_hist_json",
                                       "explore",
                                       "hist-json-task",
                                       "history json task",
                                       "prompt",
                                       "deepseek-v4-pro",
                                       "kernel/tooling",
                                       "subsystem",
                                       "coordination",
                                       NULL) == 0;
    if (ok) {
        for (int i = 0; i < 40; i++) {
            char user_text[96];
            char assistant_text[96];
            char reasoning_text[96];
            cJSON *assistant_payload = cJSON_CreateObject();
            char *payload_json = NULL;

            snprintf(user_text, sizeof(user_text), "history user %02d", i);
            snprintf(assistant_text, sizeof(assistant_text), "history assistant %02d", i);
            snprintf(reasoning_text, sizeof(reasoning_text), "history reasoning %02d", i);
            if (session_store_append("delegate_sync_hist_json", "user", user_text) != 0) {
                ok = 0;
                cJSON_Delete(assistant_payload);
                break;
            }
            if (!assistant_payload) {
                ok = 0;
                break;
            }
            cJSON_AddStringToObject(assistant_payload, "text", assistant_text);
            cJSON_AddStringToObject(assistant_payload, "reasoning", reasoning_text);
            payload_json = cJSON_PrintUnformatted(assistant_payload);
            cJSON_Delete(assistant_payload);
            if (!payload_json) {
                ok = 0;
                break;
            }
            if (session_store_append("delegate_sync_hist_json", "assistant", payload_json) != 0) {
                free(payload_json);
                ok = 0;
                break;
            }
            free(payload_json);
        }
    }
    if (ok) {
        ok = delegate_task_store_complete("dt_hist_json", "history json final summary", "", false) == 0;
    }
    if (ok) {
        ok = delegate_task_store_snapshot("dt_hist_json", &snapshot) == 0;
    }
    if (ok) {
        delegate_child_session_json_options_t options = {
            .history_limit = DELEGATE_CHILD_SESSION_HISTORY_LIMIT_DEFAULT,
        };
        cJSON *child = delegate_child_session_json_build_from_task(&snapshot, &options);
        cJSON *history = child ? cJSON_GetObjectItemCaseSensitive(child, "history") : NULL;
        cJSON *first = history && cJSON_IsArray(history) ? cJSON_GetArrayItem(history, 0) : NULL;
        cJSON *last = history && cJSON_IsArray(history) ? cJSON_GetArrayItem(history, cJSON_GetArraySize(history) - 1) : NULL;
        cJSON *first_content = first ? cJSON_GetObjectItemCaseSensitive(first, "content") : NULL;
        cJSON *last_content = last ? cJSON_GetObjectItemCaseSensitive(last, "content") : NULL;
        cJSON *last_reasoning = last ? cJSON_GetObjectItemCaseSensitive(last, "reasoning") : NULL;

        ok = child &&
             history &&
             cJSON_IsArray(history) &&
             cJSON_GetArraySize(history) == DELEGATE_CHILD_SESSION_HISTORY_LIMIT_DEFAULT &&
             cJSON_IsString(first_content) &&
             strstr(first_content->valuestring, "history user 00") != NULL &&
             cJSON_IsString(last_content) &&
             strstr(last_content->valuestring, "history assistant 39") != NULL &&
             cJSON_IsString(last_reasoning) &&
             strstr(last_reasoning->valuestring, "history reasoning 39") != NULL;

        if (!ok) {
            pr_info("  hist_json diag: history_size=%d first=%s last=%s reasoning=%s",
                    history && cJSON_IsArray(history) ? cJSON_GetArraySize(history) : -1,
                    cJSON_IsString(first_content) ? first_content->valuestring : "",
                    cJSON_IsString(last_content) ? last_content->valuestring : "",
                    cJSON_IsString(last_reasoning) ? last_reasoning->valuestring : "");
        }
        cJSON_Delete(child);
    }

    report("delegate child session json retains recent history window", ok);
}

static void test_delegate_child_session_json_exposes_normalized_session_fields(void)
{
    delegate_task_store_reset_for_test();

    delegate_task_record_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));

    int ok = delegate_task_store_start("dt_child_norm",
                                       "dc_child_norm",
                                       "delegate_sync_child_norm",
                                       "explore",
                                       "child-norm-task",
                                       "child normalized session",
                                       "prompt",
                                       "deepseek-v4-pro",
                                       "kernel/tooling",
                                       "subsystem",
                                       "coordination",
                                       NULL) == 0;
    if (ok) {
        ok = delegate_task_store_set_pending_request("dt_child_norm",
                                                     "question_text",
                                                     "question_child_norm_1",
                                                     "请确认本轮是否继续并行拆分 delegate reducer") == 0;
    }
    if (ok) {
        ok = delegate_task_store_snapshot("dt_child_norm", &snapshot) == 0;
    }
    if (ok) {
        delegate_child_session_json_options_t options = {
            .history_limit = DELEGATE_CHILD_SESSION_HISTORY_LIMIT_DEFAULT,
        };
        cJSON *child = delegate_child_session_json_build_from_task(&snapshot, &options);
        cJSON *status = child ? cJSON_GetObjectItemCaseSensitive(child, "status") : NULL;
        cJSON *pending = child ? cJSON_GetObjectItemCaseSensitive(child, "pending_request") : NULL;
        cJSON *latest = child ? cJSON_GetObjectItemCaseSensitive(child, "latest_frame") : NULL;
        cJSON *pending_type = pending ? cJSON_GetObjectItemCaseSensitive(pending, "request_type") : NULL;
        cJSON *pending_id = pending ? cJSON_GetObjectItemCaseSensitive(pending, "request_id") : NULL;
        cJSON *pending_prompt = pending ? cJSON_GetObjectItemCaseSensitive(pending, "prompt") : NULL;
        cJSON *latest_type = latest ? cJSON_GetObjectItemCaseSensitive(latest, "type") : NULL;
        cJSON *latest_status = latest ? cJSON_GetObjectItemCaseSensitive(latest, "status") : NULL;
        cJSON *latest_detail = latest ? cJSON_GetObjectItemCaseSensitive(latest, "detail") : NULL;

        ok = child &&
             cJSON_IsString(status) &&
             strcmp(status->valuestring, "running") == 0 &&
             pending &&
             cJSON_IsObject(pending) &&
             cJSON_IsString(pending_type) &&
             strcmp(pending_type->valuestring, "question_text") == 0 &&
             cJSON_IsString(pending_id) &&
             strcmp(pending_id->valuestring, "question_child_norm_1") == 0 &&
             cJSON_IsString(pending_prompt) &&
             strstr(pending_prompt->valuestring, "继续并行拆分 delegate reducer") != NULL &&
             latest &&
             cJSON_IsObject(latest) &&
             cJSON_IsString(latest_type) &&
             strcmp(latest_type->valuestring, "subagent_request") == 0 &&
             cJSON_IsString(latest_status) &&
             strcmp(latest_status->valuestring, "blocked") == 0 &&
             cJSON_IsString(latest_detail) &&
             strstr(latest_detail->valuestring, "继续并行拆分 delegate reducer") != NULL;

        if (!ok) {
            pr_info("  child_norm diag: status=%s pending_type=%s latest_type=%s latest_status=%s latest_detail=%s",
                    cJSON_IsString(status) ? status->valuestring : "",
                    cJSON_IsString(pending_type) ? pending_type->valuestring : "",
                    cJSON_IsString(latest_type) ? latest_type->valuestring : "",
                    cJSON_IsString(latest_status) ? latest_status->valuestring : "",
                    cJSON_IsString(latest_detail) ? latest_detail->valuestring : "");
        }
        cJSON_Delete(child);
    }

    report("delegate child session json exposes normalized session fields", ok);
}

static void test_delegate_child_session_json_exposes_window_metadata(void)
{
    delegate_task_store_reset_for_test();
    session_store_clear("delegate_sync_child_window");

    delegate_task_record_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));

    int ok = delegate_task_store_start("dt_child_window",
                                       "dc_child_window",
                                       "delegate_sync_child_window",
                                       "explore",
                                       "child-window-task",
                                       "child window session",
                                       "prompt",
                                       "deepseek-v4-pro",
                                       "kernel/tooling",
                                       "subsystem",
                                       "coordination",
                                       NULL) == 0;
    if (ok) {
        for (int i = 0; i < 12; i++) {
            char detail[96];
            snprintf(detail, sizeof(detail), "window step %02d", i);
            if (delegate_task_store_append_session_step("dt_child_window", "tool", detail, detail) != 0) {
                ok = 0;
                break;
            }
        }
    }
    if (ok) {
        for (int i = 0; i < 100; i++) {
            char user_text[96];
            snprintf(user_text, sizeof(user_text), "window user %02d", i);
            if (session_store_append("delegate_sync_child_window", "user", user_text) != 0) {
                ok = 0;
                break;
            }
        }
    }
    if (ok) {
        ok = delegate_task_store_complete("dt_child_window", "child window summary", "", false) == 0;
    }
    if (ok) {
        ok = delegate_task_store_snapshot("dt_child_window", &snapshot) == 0;
    }
    if (ok) {
        delegate_child_session_json_options_t options = {
            .history_limit = 8,
        };
        cJSON *child = delegate_child_session_json_build_from_task(&snapshot, &options);
        cJSON *window = child ? cJSON_GetObjectItemCaseSensitive(child, "window") : NULL;
        cJSON *cursor = child ? cJSON_GetObjectItemCaseSensitive(child, "cursor") : NULL;
        cJSON *history_cursor = cursor ? cJSON_GetObjectItemCaseSensitive(cursor, "history") : NULL;
        cJSON *frame_cursor = cursor ? cJSON_GetObjectItemCaseSensitive(cursor, "frames") : NULL;
        cJSON *commit_cursor = cursor ? cJSON_GetObjectItemCaseSensitive(cursor, "commits") : NULL;
        cJSON *history_limit = window ? cJSON_GetObjectItemCaseSensitive(window, "history_limit") : NULL;
        cJSON *history_count = window ? cJSON_GetObjectItemCaseSensitive(window, "history_count") : NULL;
        cJSON *history_total = window ? cJSON_GetObjectItemCaseSensitive(window, "history_total") : NULL;
        cJSON *history_truncated = window ? cJSON_GetObjectItemCaseSensitive(window, "history_truncated") : NULL;
        cJSON *history_first_seq = window ? cJSON_GetObjectItemCaseSensitive(window, "history_first_seq") : NULL;
        cJSON *history_last_seq = window ? cJSON_GetObjectItemCaseSensitive(window, "history_last_seq") : NULL;
        cJSON *frame_limit = window ? cJSON_GetObjectItemCaseSensitive(window, "frame_limit") : NULL;
        cJSON *frame_count = window ? cJSON_GetObjectItemCaseSensitive(window, "frame_count") : NULL;
        cJSON *frame_total = window ? cJSON_GetObjectItemCaseSensitive(window, "frame_total") : NULL;
        cJSON *frame_truncated = window ? cJSON_GetObjectItemCaseSensitive(window, "frame_truncated") : NULL;
        cJSON *frame_first_seq = window ? cJSON_GetObjectItemCaseSensitive(window, "frame_first_seq") : NULL;
        cJSON *frame_last_seq = window ? cJSON_GetObjectItemCaseSensitive(window, "frame_last_seq") : NULL;
        cJSON *commit_limit = window ? cJSON_GetObjectItemCaseSensitive(window, "commit_limit") : NULL;
        cJSON *commit_count = window ? cJSON_GetObjectItemCaseSensitive(window, "commit_count") : NULL;
        cJSON *commit_total = window ? cJSON_GetObjectItemCaseSensitive(window, "commit_total") : NULL;
        cJSON *commit_truncated = window ? cJSON_GetObjectItemCaseSensitive(window, "commit_truncated") : NULL;
        cJSON *commit_first_seq = window ? cJSON_GetObjectItemCaseSensitive(window, "commit_first_seq") : NULL;
        cJSON *commit_last_seq = window ? cJSON_GetObjectItemCaseSensitive(window, "commit_last_seq") : NULL;
        cJSON *history_cursor_after_seq = history_cursor ? cJSON_GetObjectItemCaseSensitive(history_cursor, "after_seq") : NULL;
        cJSON *history_cursor_visible_seq = history_cursor ? cJSON_GetObjectItemCaseSensitive(history_cursor, "visible_seq") : NULL;
        cJSON *history_cursor_next_seq = history_cursor ? cJSON_GetObjectItemCaseSensitive(history_cursor, "next_seq") : NULL;
        cJSON *history_cursor_high_water_seq = history_cursor ? cJSON_GetObjectItemCaseSensitive(history_cursor, "high_water_seq") : NULL;
        cJSON *history_cursor_has_more = history_cursor ? cJSON_GetObjectItemCaseSensitive(history_cursor, "has_more") : NULL;
        cJSON *frame_cursor_after_seq = frame_cursor ? cJSON_GetObjectItemCaseSensitive(frame_cursor, "after_seq") : NULL;
        cJSON *frame_cursor_visible_seq = frame_cursor ? cJSON_GetObjectItemCaseSensitive(frame_cursor, "visible_seq") : NULL;
        cJSON *frame_cursor_next_seq = frame_cursor ? cJSON_GetObjectItemCaseSensitive(frame_cursor, "next_seq") : NULL;
        cJSON *frame_cursor_high_water_seq = frame_cursor ? cJSON_GetObjectItemCaseSensitive(frame_cursor, "high_water_seq") : NULL;
        cJSON *frame_cursor_has_more = frame_cursor ? cJSON_GetObjectItemCaseSensitive(frame_cursor, "has_more") : NULL;
        cJSON *commit_cursor_after_seq = commit_cursor ? cJSON_GetObjectItemCaseSensitive(commit_cursor, "after_seq") : NULL;
        cJSON *commit_cursor_visible_seq = commit_cursor ? cJSON_GetObjectItemCaseSensitive(commit_cursor, "visible_seq") : NULL;
        cJSON *commit_cursor_next_seq = commit_cursor ? cJSON_GetObjectItemCaseSensitive(commit_cursor, "next_seq") : NULL;
        cJSON *commit_cursor_high_water_seq = commit_cursor ? cJSON_GetObjectItemCaseSensitive(commit_cursor, "high_water_seq") : NULL;
        cJSON *commit_cursor_has_more = commit_cursor ? cJSON_GetObjectItemCaseSensitive(commit_cursor, "has_more") : NULL;

        ok = child &&
             window &&
             cursor &&
             cJSON_IsObject(cursor) &&
             history_cursor &&
             cJSON_IsObject(history_cursor) &&
             frame_cursor &&
             cJSON_IsObject(frame_cursor) &&
             commit_cursor &&
             cJSON_IsObject(commit_cursor) &&
             cJSON_IsObject(window) &&
             cJSON_IsNumber(history_limit) &&
             history_limit->valuedouble == 8 &&
             cJSON_IsNumber(history_count) &&
             history_count->valuedouble == 8 &&
             cJSON_IsNumber(history_total) &&
             history_total->valuedouble >= 100 &&
             cJSON_IsBool(history_truncated) &&
             cJSON_IsTrue(history_truncated) &&
             cJSON_IsNumber(history_first_seq) &&
             history_first_seq->valuedouble == 93 &&
             cJSON_IsNumber(history_last_seq) &&
             history_last_seq->valuedouble == 100 &&
             cJSON_IsNumber(frame_limit) &&
             frame_limit->valuedouble == DELEGATE_SESSION_FRAME_LIMIT &&
             cJSON_IsNumber(frame_count) &&
             frame_count->valuedouble == 14 &&
             cJSON_IsNumber(frame_total) &&
             frame_total->valuedouble == 14 &&
             cJSON_IsBool(frame_truncated) &&
             cJSON_IsFalse(frame_truncated) &&
             cJSON_IsNumber(frame_first_seq) &&
             frame_first_seq->valuedouble == 1 &&
             cJSON_IsNumber(frame_last_seq) &&
             frame_last_seq->valuedouble == 14 &&
             cJSON_IsNumber(commit_limit) &&
             commit_limit->valuedouble == DELEGATE_SESSION_COMMIT_LIMIT &&
             cJSON_IsNumber(commit_count) &&
             commit_count->valuedouble == 14 &&
             cJSON_IsNumber(commit_total) &&
             commit_total->valuedouble == 14 &&
             cJSON_IsBool(commit_truncated) &&
             cJSON_IsFalse(commit_truncated) &&
             cJSON_IsNumber(commit_first_seq) &&
             commit_first_seq->valuedouble == 1 &&
             cJSON_IsNumber(commit_last_seq) &&
             commit_last_seq->valuedouble == 14 &&
             cJSON_IsNumber(history_cursor_after_seq) &&
             history_cursor_after_seq->valuedouble == 0 &&
             cJSON_IsNumber(history_cursor_visible_seq) &&
             history_cursor_visible_seq->valuedouble == 100 &&
             cJSON_IsNumber(history_cursor_next_seq) &&
             history_cursor_next_seq->valuedouble == 101 &&
             cJSON_IsNumber(history_cursor_high_water_seq) &&
             history_cursor_high_water_seq->valuedouble == 100 &&
             cJSON_IsBool(history_cursor_has_more) &&
             cJSON_IsFalse(history_cursor_has_more) &&
             cJSON_IsNumber(frame_cursor_after_seq) &&
             frame_cursor_after_seq->valuedouble == 0 &&
             cJSON_IsNumber(frame_cursor_visible_seq) &&
             frame_cursor_visible_seq->valuedouble == 14 &&
             cJSON_IsNumber(frame_cursor_next_seq) &&
             frame_cursor_next_seq->valuedouble == 15 &&
             cJSON_IsNumber(frame_cursor_high_water_seq) &&
             frame_cursor_high_water_seq->valuedouble == 14 &&
             cJSON_IsBool(frame_cursor_has_more) &&
             cJSON_IsFalse(frame_cursor_has_more) &&
             cJSON_IsNumber(commit_cursor_after_seq) &&
             commit_cursor_after_seq->valuedouble == 0 &&
             cJSON_IsNumber(commit_cursor_visible_seq) &&
             commit_cursor_visible_seq->valuedouble == 14 &&
             cJSON_IsNumber(commit_cursor_next_seq) &&
             commit_cursor_next_seq->valuedouble == 15 &&
             cJSON_IsNumber(commit_cursor_high_water_seq) &&
             commit_cursor_high_water_seq->valuedouble == 14 &&
             cJSON_IsBool(commit_cursor_has_more) &&
             cJSON_IsFalse(commit_cursor_has_more);

        if (!ok) {
            char *cursor_json = cursor ? cJSON_PrintUnformatted(cursor) : NULL;
            pr_info("  child_window diag: history_limit=%f history_count=%f history_total=%f history_truncated=%d history_first_seq=%f history_last_seq=%f frame_limit=%f frame_count=%f frame_total=%f frame_truncated=%d frame_first_seq=%f frame_last_seq=%f commit_limit=%f commit_count=%f commit_total=%f commit_truncated=%d commit_first_seq=%f commit_last_seq=%f hist_cursor_after=%f hist_cursor_visible=%f hist_cursor_next=%f hist_cursor_high=%f hist_cursor_more=%d frame_cursor_after=%f frame_cursor_visible=%f frame_cursor_next=%f frame_cursor_high=%f frame_cursor_more=%d commit_cursor_after=%f commit_cursor_visible=%f commit_cursor_next=%f commit_cursor_high=%f commit_cursor_more=%d",
                    cJSON_IsNumber(history_limit) ? history_limit->valuedouble : -1.0,
                    cJSON_IsNumber(history_count) ? history_count->valuedouble : -1.0,
                    cJSON_IsNumber(history_total) ? history_total->valuedouble : -1.0,
                    cJSON_IsBool(history_truncated) ? cJSON_IsTrue(history_truncated) : -1,
                    cJSON_IsNumber(history_first_seq) ? history_first_seq->valuedouble : -1.0,
                    cJSON_IsNumber(history_last_seq) ? history_last_seq->valuedouble : -1.0,
                    cJSON_IsNumber(frame_limit) ? frame_limit->valuedouble : -1.0,
                    cJSON_IsNumber(frame_count) ? frame_count->valuedouble : -1.0,
                    cJSON_IsNumber(frame_total) ? frame_total->valuedouble : -1.0,
                    cJSON_IsBool(frame_truncated) ? cJSON_IsTrue(frame_truncated) : -1,
                    cJSON_IsNumber(frame_first_seq) ? frame_first_seq->valuedouble : -1.0,
                    cJSON_IsNumber(frame_last_seq) ? frame_last_seq->valuedouble : -1.0,
                    cJSON_IsNumber(commit_limit) ? commit_limit->valuedouble : -1.0,
                    cJSON_IsNumber(commit_count) ? commit_count->valuedouble : -1.0,
                    cJSON_IsNumber(commit_total) ? commit_total->valuedouble : -1.0,
                    cJSON_IsBool(commit_truncated) ? cJSON_IsTrue(commit_truncated) : -1,
                    cJSON_IsNumber(commit_first_seq) ? commit_first_seq->valuedouble : -1.0,
                    cJSON_IsNumber(commit_last_seq) ? commit_last_seq->valuedouble : -1.0,
                    cJSON_IsNumber(history_cursor_after_seq) ? history_cursor_after_seq->valuedouble : -1.0,
                    cJSON_IsNumber(history_cursor_visible_seq) ? history_cursor_visible_seq->valuedouble : -1.0,
                    cJSON_IsNumber(history_cursor_next_seq) ? history_cursor_next_seq->valuedouble : -1.0,
                    cJSON_IsNumber(history_cursor_high_water_seq) ? history_cursor_high_water_seq->valuedouble : -1.0,
                    cJSON_IsBool(history_cursor_has_more) ? cJSON_IsTrue(history_cursor_has_more) : -1,
                    cJSON_IsNumber(frame_cursor_after_seq) ? frame_cursor_after_seq->valuedouble : -1.0,
                    cJSON_IsNumber(frame_cursor_visible_seq) ? frame_cursor_visible_seq->valuedouble : -1.0,
                    cJSON_IsNumber(frame_cursor_next_seq) ? frame_cursor_next_seq->valuedouble : -1.0,
                    cJSON_IsNumber(frame_cursor_high_water_seq) ? frame_cursor_high_water_seq->valuedouble : -1.0,
                    cJSON_IsBool(frame_cursor_has_more) ? cJSON_IsTrue(frame_cursor_has_more) : -1,
                    cJSON_IsNumber(commit_cursor_after_seq) ? commit_cursor_after_seq->valuedouble : -1.0,
                    cJSON_IsNumber(commit_cursor_visible_seq) ? commit_cursor_visible_seq->valuedouble : -1.0,
                    cJSON_IsNumber(commit_cursor_next_seq) ? commit_cursor_next_seq->valuedouble : -1.0,
                    cJSON_IsNumber(commit_cursor_high_water_seq) ? commit_cursor_high_water_seq->valuedouble : -1.0,
                    cJSON_IsBool(commit_cursor_has_more) ? cJSON_IsTrue(commit_cursor_has_more) : -1);
            pr_info("  child_window cursor json: %s", cursor_json ? cursor_json : "<null>");
            free(cursor_json);
        }
        cJSON_Delete(child);
    }

    report("delegate child session json exposes window metadata", ok);
}

static void test_delegate_child_session_json_history_exposes_timestamps(void)
{
    delegate_task_store_reset_for_test();
    session_store_clear("delegate_sync_hist_ts");

    delegate_task_record_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));

    int ok = delegate_task_store_start("dt_hist_ts",
                                       "dc_hist_ts",
                                       "delegate_sync_hist_ts",
                                       "explore",
                                       "hist-ts-task",
                                       "history timestamp task",
                                       "prompt",
                                       "deepseek-v4-pro",
                                       "kernel/tooling",
                                       "subsystem",
                                       "coordination",
                                       NULL) == 0;
    if (ok) {
        cJSON *assistant_payload = cJSON_CreateObject();
        char *payload_json = NULL;
        ok = session_store_append("delegate_sync_hist_ts", "user", "timestamped user message") == 0;
        if (ok && assistant_payload) {
            cJSON_AddStringToObject(assistant_payload, "text", "timestamped assistant message");
            cJSON_AddStringToObject(assistant_payload, "reasoning", "timestamped assistant reasoning");
            payload_json = cJSON_PrintUnformatted(assistant_payload);
            cJSON_Delete(assistant_payload);
            if (!payload_json) {
                ok = 0;
            } else {
                ok = session_store_append("delegate_sync_hist_ts", "assistant", payload_json) == 0;
                free(payload_json);
            }
        } else {
            cJSON_Delete(assistant_payload);
        }
    }
    if (ok) {
        ok = delegate_task_store_complete("dt_hist_ts", "history timestamp summary", "", false) == 0;
    }
    if (ok) {
        ok = delegate_task_store_snapshot("dt_hist_ts", &snapshot) == 0;
    }
    if (ok) {
        delegate_child_session_json_options_t options = {
            .history_limit = 8,
        };
        cJSON *child = delegate_child_session_json_build_from_task(&snapshot, &options);
        cJSON *history = child ? cJSON_GetObjectItemCaseSensitive(child, "history") : NULL;
        cJSON *first = history && cJSON_IsArray(history) ? cJSON_GetArrayItem(history, 0) : NULL;
        cJSON *second = history && cJSON_IsArray(history) ? cJSON_GetArrayItem(history, 1) : NULL;
        cJSON *first_ts = first ? cJSON_GetObjectItemCaseSensitive(first, "ts") : NULL;
        cJSON *second_ts = second ? cJSON_GetObjectItemCaseSensitive(second, "ts") : NULL;

        ok = child &&
             history &&
             cJSON_IsArray(history) &&
             cJSON_GetArraySize(history) >= 2 &&
             cJSON_IsNumber(first_ts) &&
             cJSON_IsNumber(second_ts) &&
             first_ts->valuedouble > 0 &&
             second_ts->valuedouble >= first_ts->valuedouble;

        if (!ok) {
            pr_info("  hist_ts diag: history_size=%d first_ts=%f second_ts=%f",
                    history && cJSON_IsArray(history) ? cJSON_GetArraySize(history) : -1,
                    cJSON_IsNumber(first_ts) ? first_ts->valuedouble : -1.0,
                    cJSON_IsNumber(second_ts) ? second_ts->valuedouble : -1.0);
        }
        cJSON_Delete(child);
    }

    report("delegate child session json history exposes timestamps", ok);
}

static void test_delegate_child_session_json_history_exposes_sources(void)
{
    delegate_task_store_reset_for_test();
    session_store_clear("delegate_sync_hist_source");

    delegate_task_record_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));

    int ok = delegate_task_store_start("dt_hist_source",
                                       "dc_hist_source",
                                       "delegate_sync_hist_source",
                                       "explore",
                                       "hist-source-task",
                                       "history source task",
                                       "prompt",
                                       "deepseek-v4-pro",
                                       "kernel/tooling",
                                       "subsystem",
                                       "coordination",
                                       NULL) == 0;
    if (ok) {
        ok = session_store_append_ex("delegate_sync_hist_source",
                                     "user",
                                     "source-tagged user message",
                                     "user") == 0;
    }
    if (ok) {
        cJSON *assistant_payload = cJSON_CreateObject();
        char *payload_json = NULL;
        if (!assistant_payload) {
            ok = 0;
        } else {
            cJSON_AddStringToObject(assistant_payload, "text", "source-tagged assistant message");
            cJSON_AddStringToObject(assistant_payload, "reasoning", "source-tagged assistant reasoning");
            payload_json = cJSON_PrintUnformatted(assistant_payload);
            cJSON_Delete(assistant_payload);
            if (!payload_json) {
                ok = 0;
            } else {
                ok = session_store_append_ex("delegate_sync_hist_source",
                                             "assistant",
                                             payload_json,
                                             "delegate_child") == 0;
                free(payload_json);
            }
        }
    }
    if (ok) {
        ok = delegate_task_store_complete("dt_hist_source", "history source summary", "", false) == 0;
    }
    if (ok) {
        ok = delegate_task_store_snapshot("dt_hist_source", &snapshot) == 0;
    }
    if (ok) {
        delegate_child_session_json_options_t options = {
            .history_limit = 8,
        };
        cJSON *child = delegate_child_session_json_build_from_task(&snapshot, &options);
        cJSON *history = child ? cJSON_GetObjectItemCaseSensitive(child, "history") : NULL;
        cJSON *first = history && cJSON_IsArray(history) ? cJSON_GetArrayItem(history, 0) : NULL;
        cJSON *second = history && cJSON_IsArray(history) ? cJSON_GetArrayItem(history, 1) : NULL;
        cJSON *first_source = first ? cJSON_GetObjectItemCaseSensitive(first, "source") : NULL;
        cJSON *second_source = second ? cJSON_GetObjectItemCaseSensitive(second, "source") : NULL;

        ok = child &&
             history &&
             cJSON_IsArray(history) &&
             cJSON_GetArraySize(history) >= 2 &&
             cJSON_IsString(first_source) &&
             strcmp(first_source->valuestring, "user") == 0 &&
             cJSON_IsString(second_source) &&
             strcmp(second_source->valuestring, "delegate_child") == 0;

        if (!ok) {
            pr_info("  hist_source diag: history_size=%d first_source=%s second_source=%s",
                    history && cJSON_IsArray(history) ? cJSON_GetArraySize(history) : -1,
                    cJSON_IsString(first_source) ? first_source->valuestring : "",
                    cJSON_IsString(second_source) ? second_source->valuestring : "");
        }
        cJSON_Delete(child);
    }

    report("delegate child session json history exposes sources", ok);
}

static void test_delegate_child_session_json_history_exposes_ids(void)
{
    delegate_task_store_reset_for_test();
    session_store_clear("delegate_sync_hist_id");

    delegate_task_record_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));

    int ok = delegate_task_store_start("dt_hist_id",
                                       "dc_hist_id",
                                       "delegate_sync_hist_id",
                                       "explore",
                                       "hist-id-task",
                                       "history id task",
                                       "prompt",
                                       "deepseek-v4-pro",
                                       "kernel/tooling",
                                       "subsystem",
                                       "coordination",
                                       NULL) == 0;
    if (ok) {
        ok = session_store_append_ex("delegate_sync_hist_id",
                                     "user",
                                     "stable-id user message",
                                     "user") == 0;
    }
    if (ok) {
        cJSON *assistant_payload = cJSON_CreateObject();
        char *payload_json = NULL;
        if (!assistant_payload) {
            ok = 0;
        } else {
            cJSON_AddStringToObject(assistant_payload, "text", "stable-id assistant message");
            cJSON_AddStringToObject(assistant_payload, "reasoning", "stable-id assistant reasoning");
            payload_json = cJSON_PrintUnformatted(assistant_payload);
            cJSON_Delete(assistant_payload);
            if (!payload_json) {
                ok = 0;
            } else {
                ok = session_store_append_ex("delegate_sync_hist_id",
                                             "assistant",
                                             payload_json,
                                             "delegate_child") == 0;
                free(payload_json);
            }
        }
    }
    if (ok) {
        ok = delegate_task_store_complete("dt_hist_id", "history id summary", "", false) == 0;
    }
    if (ok) {
        ok = delegate_task_store_snapshot("dt_hist_id", &snapshot) == 0;
    }
    if (ok) {
        delegate_child_session_json_options_t options = {
            .history_limit = 8,
        };
        cJSON *child = delegate_child_session_json_build_from_task(&snapshot, &options);
        cJSON *history = child ? cJSON_GetObjectItemCaseSensitive(child, "history") : NULL;
        cJSON *first = history && cJSON_IsArray(history) ? cJSON_GetArrayItem(history, 0) : NULL;
        cJSON *second = history && cJSON_IsArray(history) ? cJSON_GetArrayItem(history, 1) : NULL;
        cJSON *first_id = first ? cJSON_GetObjectItemCaseSensitive(first, "id") : NULL;
        cJSON *second_id = second ? cJSON_GetObjectItemCaseSensitive(second, "id") : NULL;

        ok = child &&
             history &&
             cJSON_IsArray(history) &&
             cJSON_GetArraySize(history) >= 2 &&
             cJSON_IsString(first_id) &&
             first_id->valuestring[0] &&
             cJSON_IsString(second_id) &&
             second_id->valuestring[0] &&
             strcmp(first_id->valuestring, second_id->valuestring) != 0;

        if (!ok) {
            pr_info("  hist_id diag: history_size=%d first_id=%s second_id=%s",
                    history && cJSON_IsArray(history) ? cJSON_GetArraySize(history) : -1,
                    cJSON_IsString(first_id) ? first_id->valuestring : "",
                    cJSON_IsString(second_id) ? second_id->valuestring : "");
        }
        cJSON_Delete(child);
    }

    report("delegate child session json history exposes ids", ok);
}

static void test_delegate_child_session_json_history_exposes_seq(void)
{
    session_store_clear("delegate_sync_hist_seq");
    delegate_task_store_reset_for_test();

    delegate_task_record_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));

    int ok = delegate_task_store_start("dt_hist_seq",
                                       "dc_hist_seq",
                                       "delegate_sync_hist_seq",
                                       "explore",
                                       "history-seq-task",
                                       "history seq task",
                                       "prompt",
                                       "deepseek-v4-pro",
                                       "kernel/tooling",
                                       "subsystem",
                                       "coordination",
                                       NULL) == 0;
    if (ok) {
        ok = session_store_append_ex("delegate_sync_hist_seq",
                                     "user",
                                     "history seq user message",
                                     "user") == 0;
    }
    if (ok) {
        ok = session_store_append_ex("delegate_sync_hist_seq",
                                     "assistant",
                                     "history seq assistant message",
                                     "delegate_child") == 0;
    }
    if (ok) {
        ok = delegate_task_store_complete("dt_hist_seq", "history seq summary", "", false) == 0;
    }
    if (ok) {
        ok = delegate_task_store_snapshot("dt_hist_seq", &snapshot) == 0;
    }
    if (ok) {
        delegate_child_session_json_options_t options = {
            .history_limit = 8,
        };
        cJSON *child = delegate_child_session_json_build_from_task(&snapshot, &options);
        cJSON *history = child ? cJSON_GetObjectItemCaseSensitive(child, "history") : NULL;
        cJSON *first = history && cJSON_IsArray(history) ? cJSON_GetArrayItem(history, 0) : NULL;
        cJSON *second = history && cJSON_IsArray(history) ? cJSON_GetArrayItem(history, 1) : NULL;
        cJSON *first_seq = first ? cJSON_GetObjectItemCaseSensitive(first, "seq") : NULL;
        cJSON *second_seq = second ? cJSON_GetObjectItemCaseSensitive(second, "seq") : NULL;

        ok = child &&
             history &&
             cJSON_IsArray(history) &&
             cJSON_GetArraySize(history) >= 2 &&
             cJSON_IsNumber(first_seq) &&
             cJSON_IsNumber(second_seq) &&
             first_seq->valuedouble > 0 &&
             second_seq->valuedouble > first_seq->valuedouble;

        if (!ok) {
            pr_info("  hist_seq diag: history_size=%d first_seq=%lld second_seq=%lld",
                    history && cJSON_IsArray(history) ? cJSON_GetArraySize(history) : -1,
                    cJSON_IsNumber(first_seq) ? (long long)first_seq->valuedouble : -1LL,
                    cJSON_IsNumber(second_seq) ? (long long)second_seq->valuedouble : -1LL);
        }
        cJSON_Delete(child);
    }

    report("delegate child session json history exposes seq", ok);
}

static void test_delegate_child_session_json_history_seq_stays_monotonic_across_window(void)
{
    session_store_clear("delegate_sync_hist_seq_window");
    delegate_task_store_reset_for_test();

    delegate_task_record_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));

    int ok = delegate_task_store_start("dt_hist_seq_window",
                                       "dc_hist_seq_window",
                                       "delegate_sync_hist_seq_window",
                                       "explore",
                                       "history-seq-window-task",
                                       "history seq window task",
                                       "prompt",
                                       "deepseek-v4-pro",
                                       "kernel/tooling",
                                       "subsystem",
                                       "coordination",
                                       NULL) == 0;
    if (ok) {
        for (int i = 0; i < 10; i++) {
            char content[96];
            snprintf(content, sizeof(content), "history seq window message %02d", i);
            if (session_store_append_ex("delegate_sync_hist_seq_window",
                                        (i % 2) == 0 ? "user" : "assistant",
                                        content,
                                        (i % 2) == 0 ? "user" : "delegate_child") != 0) {
                ok = 0;
                break;
            }
        }
    }
    if (ok) {
        ok = delegate_task_store_complete("dt_hist_seq_window", "history seq window summary", "", false) == 0;
    }
    if (ok) {
        ok = delegate_task_store_snapshot("dt_hist_seq_window", &snapshot) == 0;
    }
    if (ok) {
        delegate_child_session_json_options_t options = {
            .history_limit = 4,
        };
        cJSON *child = delegate_child_session_json_build_from_task(&snapshot, &options);
        cJSON *history = child ? cJSON_GetObjectItemCaseSensitive(child, "history") : NULL;
        cJSON *first = history && cJSON_IsArray(history) ? cJSON_GetArrayItem(history, 0) : NULL;
        cJSON *last = history && cJSON_IsArray(history) ? cJSON_GetArrayItem(history, cJSON_GetArraySize(history) - 1) : NULL;
        cJSON *first_seq = first ? cJSON_GetObjectItemCaseSensitive(first, "seq") : NULL;
        cJSON *last_seq = last ? cJSON_GetObjectItemCaseSensitive(last, "seq") : NULL;

        ok = child &&
             history &&
             cJSON_IsArray(history) &&
             cJSON_GetArraySize(history) == 4 &&
             cJSON_IsNumber(first_seq) &&
             cJSON_IsNumber(last_seq) &&
             (long long)first_seq->valuedouble == 7 &&
             (long long)last_seq->valuedouble == 10;

        if (!ok) {
            pr_info("  hist_seq_window diag: size=%d first_seq=%lld last_seq=%lld",
                    history && cJSON_IsArray(history) ? cJSON_GetArraySize(history) : -1,
                    cJSON_IsNumber(first_seq) ? (long long)first_seq->valuedouble : -1LL,
                    cJSON_IsNumber(last_seq) ? (long long)last_seq->valuedouble : -1LL);
        }
        cJSON_Delete(child);
    }

    report("delegate child session json history seq stays monotonic across window", ok);
}

static void test_delegate_child_session_json_filters_incremental_after_seq(void)
{
    session_store_clear("delegate_sync_hist_delta");
    delegate_task_store_reset_for_test();

    delegate_task_record_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));

    int ok = delegate_task_store_start("dt_hist_delta",
                                       "dc_hist_delta",
                                       "delegate_sync_hist_delta",
                                       "explore",
                                       "history-delta-task",
                                       "history delta task",
                                       "prompt",
                                       "deepseek-v4-pro",
                                       "kernel/tooling",
                                       "subsystem",
                                       "coordination",
                                       NULL) == 0;
    if (ok) {
        for (int i = 0; i < 6; i++) {
            char content[96];
            snprintf(content, sizeof(content), "history delta message %02d", i);
            if (session_store_append_ex("delegate_sync_hist_delta",
                                        (i % 2) == 0 ? "user" : "assistant",
                                        content,
                                        (i % 2) == 0 ? "user" : "delegate_child") != 0) {
                ok = 0;
                break;
            }
        }
    }
    if (ok) {
        for (int i = 0; i < 4; i++) {
            char detail[96];
            snprintf(detail, sizeof(detail), "delta step %02d", i);
            if (delegate_task_store_append_session_step("dt_hist_delta", "tool", detail, detail) != 0) {
                ok = 0;
                break;
            }
        }
    }
    if (ok) {
        ok = delegate_task_store_complete("dt_hist_delta", "history delta summary", "", false) == 0;
    }
    if (ok) {
        ok = delegate_task_store_snapshot("dt_hist_delta", &snapshot) == 0;
    }
    if (ok) {
        delegate_child_session_json_options_t options = {
            .history_limit = 8,
            .history_after_seq = 4,
            .frame_after_seq = 2,
            .commit_after_seq = 2,
        };
        cJSON *child = delegate_child_session_json_build_from_task(&snapshot, &options);
        cJSON *history = child ? cJSON_GetObjectItemCaseSensitive(child, "history") : NULL;
        cJSON *frames = child ? cJSON_GetObjectItemCaseSensitive(child, "frames") : NULL;
        cJSON *commits = child ? cJSON_GetObjectItemCaseSensitive(child, "commits") : NULL;
        cJSON *window = child ? cJSON_GetObjectItemCaseSensitive(child, "window") : NULL;
        cJSON *history_first = history && cJSON_IsArray(history) ? cJSON_GetArrayItem(history, 0) : NULL;
        cJSON *frame_first = frames && cJSON_IsArray(frames) ? cJSON_GetArrayItem(frames, 0) : NULL;
        cJSON *commit_first = commits && cJSON_IsArray(commits) ? cJSON_GetArrayItem(commits, 0) : NULL;
        cJSON *history_first_seq = history_first ? cJSON_GetObjectItemCaseSensitive(history_first, "seq") : NULL;
        cJSON *frame_first_seq = frame_first ? cJSON_GetObjectItemCaseSensitive(frame_first, "seq") : NULL;
        cJSON *commit_first_seq = commit_first ? cJSON_GetObjectItemCaseSensitive(commit_first, "seq") : NULL;
        cJSON *history_after_seq = window ? cJSON_GetObjectItemCaseSensitive(window, "history_after_seq") : NULL;
        cJSON *frame_after_seq = window ? cJSON_GetObjectItemCaseSensitive(window, "frame_after_seq") : NULL;
        cJSON *commit_after_seq = window ? cJSON_GetObjectItemCaseSensitive(window, "commit_after_seq") : NULL;
        cJSON *replay_reset = window ? cJSON_GetObjectItemCaseSensitive(window, "replay_reset") : NULL;
        cJSON *cursor = child ? cJSON_GetObjectItemCaseSensitive(child, "cursor") : NULL;
        cJSON *history_cursor = cursor ? cJSON_GetObjectItemCaseSensitive(cursor, "history") : NULL;
        cJSON *frame_cursor = cursor ? cJSON_GetObjectItemCaseSensitive(cursor, "frames") : NULL;
        cJSON *commit_cursor = cursor ? cJSON_GetObjectItemCaseSensitive(cursor, "commits") : NULL;
        cJSON *history_next_seq = history_cursor ? cJSON_GetObjectItemCaseSensitive(history_cursor, "next_seq") : NULL;
        cJSON *history_high_water_seq = history_cursor ? cJSON_GetObjectItemCaseSensitive(history_cursor, "high_water_seq") : NULL;
        cJSON *history_has_more = history_cursor ? cJSON_GetObjectItemCaseSensitive(history_cursor, "has_more") : NULL;
        cJSON *frame_next_seq = frame_cursor ? cJSON_GetObjectItemCaseSensitive(frame_cursor, "next_seq") : NULL;
        cJSON *frame_high_water_seq = frame_cursor ? cJSON_GetObjectItemCaseSensitive(frame_cursor, "high_water_seq") : NULL;
        cJSON *frame_has_more = frame_cursor ? cJSON_GetObjectItemCaseSensitive(frame_cursor, "has_more") : NULL;
        cJSON *commit_next_seq = commit_cursor ? cJSON_GetObjectItemCaseSensitive(commit_cursor, "next_seq") : NULL;
        cJSON *commit_high_water_seq = commit_cursor ? cJSON_GetObjectItemCaseSensitive(commit_cursor, "high_water_seq") : NULL;
        cJSON *commit_has_more = commit_cursor ? cJSON_GetObjectItemCaseSensitive(commit_cursor, "has_more") : NULL;

        ok = child &&
             history && cJSON_IsArray(history) && cJSON_GetArraySize(history) == 2 &&
             frames && cJSON_IsArray(frames) && cJSON_GetArraySize(frames) == 4 &&
             commits && cJSON_IsArray(commits) && cJSON_GetArraySize(commits) == 4 &&
             cJSON_IsNumber(history_first_seq) && (long long)history_first_seq->valuedouble == 5 &&
             cJSON_IsNumber(frame_first_seq) && (long long)frame_first_seq->valuedouble == 3 &&
             cJSON_IsNumber(commit_first_seq) && (long long)commit_first_seq->valuedouble == 3 &&
             cJSON_IsNumber(history_after_seq) && (long long)history_after_seq->valuedouble == 4 &&
             cJSON_IsNumber(frame_after_seq) && (long long)frame_after_seq->valuedouble == 2 &&
             cJSON_IsNumber(commit_after_seq) && (long long)commit_after_seq->valuedouble == 2 &&
             cJSON_IsNumber(history_next_seq) && (long long)history_next_seq->valuedouble == 7 &&
             cJSON_IsNumber(history_high_water_seq) && (long long)history_high_water_seq->valuedouble == 6 &&
             cJSON_IsBool(history_has_more) && cJSON_IsFalse(history_has_more) &&
             cJSON_IsNumber(frame_next_seq) && (long long)frame_next_seq->valuedouble == 7 &&
             cJSON_IsNumber(frame_high_water_seq) && (long long)frame_high_water_seq->valuedouble == 6 &&
             cJSON_IsBool(frame_has_more) && cJSON_IsFalse(frame_has_more) &&
             cJSON_IsNumber(commit_next_seq) && (long long)commit_next_seq->valuedouble == 7 &&
             cJSON_IsNumber(commit_high_water_seq) && (long long)commit_high_water_seq->valuedouble == 6 &&
             cJSON_IsBool(commit_has_more) && cJSON_IsFalse(commit_has_more) &&
             cJSON_IsBool(replay_reset) && cJSON_IsFalse(replay_reset);

        if (!ok) {
            char *cursor_json = cursor ? cJSON_PrintUnformatted(cursor) : NULL;
            pr_info("  child_delta diag: history_size=%d frame_size=%d commit_size=%d history_first_seq=%lld frame_first_seq=%lld commit_first_seq=%lld history_after=%lld frame_after=%lld commit_after=%lld history_next=%lld history_high=%lld history_more=%d frame_next=%lld frame_high=%lld frame_more=%d commit_next=%lld commit_high=%lld commit_more=%d replay_reset=%d",
                    history && cJSON_IsArray(history) ? cJSON_GetArraySize(history) : -1,
                    frames && cJSON_IsArray(frames) ? cJSON_GetArraySize(frames) : -1,
                    commits && cJSON_IsArray(commits) ? cJSON_GetArraySize(commits) : -1,
                    cJSON_IsNumber(history_first_seq) ? (long long)history_first_seq->valuedouble : -1LL,
                    cJSON_IsNumber(frame_first_seq) ? (long long)frame_first_seq->valuedouble : -1LL,
                    cJSON_IsNumber(commit_first_seq) ? (long long)commit_first_seq->valuedouble : -1LL,
                    cJSON_IsNumber(history_after_seq) ? (long long)history_after_seq->valuedouble : -1LL,
                    cJSON_IsNumber(frame_after_seq) ? (long long)frame_after_seq->valuedouble : -1LL,
                    cJSON_IsNumber(commit_after_seq) ? (long long)commit_after_seq->valuedouble : -1LL,
                    cJSON_IsNumber(history_next_seq) ? (long long)history_next_seq->valuedouble : -1LL,
                    cJSON_IsNumber(history_high_water_seq) ? (long long)history_high_water_seq->valuedouble : -1LL,
                    cJSON_IsBool(history_has_more) ? cJSON_IsTrue(history_has_more) : -1,
                    cJSON_IsNumber(frame_next_seq) ? (long long)frame_next_seq->valuedouble : -1LL,
                    cJSON_IsNumber(frame_high_water_seq) ? (long long)frame_high_water_seq->valuedouble : -1LL,
                    cJSON_IsBool(frame_has_more) ? cJSON_IsTrue(frame_has_more) : -1,
                    cJSON_IsNumber(commit_next_seq) ? (long long)commit_next_seq->valuedouble : -1LL,
                    cJSON_IsNumber(commit_high_water_seq) ? (long long)commit_high_water_seq->valuedouble : -1LL,
                    cJSON_IsBool(commit_has_more) ? cJSON_IsTrue(commit_has_more) : -1,
                    cJSON_IsBool(replay_reset) ? cJSON_IsTrue(replay_reset) : -1);
            pr_info("  child_delta cursor json: %s", cursor_json ? cursor_json : "<null>");
            free(cursor_json);
        }
        cJSON_Delete(child);
    }

    report("delegate child session json filters incremental after seq", ok);
}

static void test_delegate_child_session_json_marks_replay_reset_when_after_seq_falls_outside_window(void)
{
    session_store_clear("delegate_sync_hist_reset");
    delegate_task_store_reset_for_test();

    delegate_task_record_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));

    int ok = delegate_task_store_start("dt_hist_reset",
                                       "dc_hist_reset",
                                       "delegate_sync_hist_reset",
                                       "explore",
                                       "history-reset-task",
                                       "history reset task",
                                       "prompt",
                                       "deepseek-v4-pro",
                                       "kernel/tooling",
                                       "subsystem",
                                       "coordination",
                                       NULL) == 0;
    if (ok) {
        for (int i = 0; i < 10; i++) {
            char content[96];
            snprintf(content, sizeof(content), "history reset message %02d", i);
            if (session_store_append_ex("delegate_sync_hist_reset",
                                        (i % 2) == 0 ? "user" : "assistant",
                                        content,
                                        (i % 2) == 0 ? "user" : "delegate_child") != 0) {
                ok = 0;
                break;
            }
        }
    }
    if (ok) {
        ok = delegate_task_store_complete("dt_hist_reset", "history reset summary", "", false) == 0;
    }
    if (ok) {
        ok = delegate_task_store_snapshot("dt_hist_reset", &snapshot) == 0;
    }
    if (ok) {
        delegate_child_session_json_options_t options = {
            .history_limit = 4,
            .history_after_seq = 4,
        };
        cJSON *child = delegate_child_session_json_build_from_task(&snapshot, &options);
        cJSON *history = child ? cJSON_GetObjectItemCaseSensitive(child, "history") : NULL;
        cJSON *window = child ? cJSON_GetObjectItemCaseSensitive(child, "window") : NULL;
        cJSON *history_first = history && cJSON_IsArray(history) ? cJSON_GetArrayItem(history, 0) : NULL;
        cJSON *history_first_seq = history_first ? cJSON_GetObjectItemCaseSensitive(history_first, "seq") : NULL;
        cJSON *replay_reset = window ? cJSON_GetObjectItemCaseSensitive(window, "replay_reset") : NULL;

        ok = child &&
             history && cJSON_IsArray(history) && cJSON_GetArraySize(history) == 4 &&
             cJSON_IsNumber(history_first_seq) && (long long)history_first_seq->valuedouble == 7 &&
             cJSON_IsBool(replay_reset) && cJSON_IsTrue(replay_reset);

        if (!ok) {
            pr_info("  child_reset diag: history_size=%d history_first_seq=%lld replay_reset=%d",
                    history && cJSON_IsArray(history) ? cJSON_GetArraySize(history) : -1,
                    cJSON_IsNumber(history_first_seq) ? (long long)history_first_seq->valuedouble : -1LL,
                    cJSON_IsBool(replay_reset) ? cJSON_IsTrue(replay_reset) : -1);
        }
        cJSON_Delete(child);
    }

    report("delegate child session json marks replay reset when after seq falls outside window", ok);
}

static void test_delegate_child_session_json_frames_and_commits_expose_ids(void)
{
    delegate_task_store_reset_for_test();

    delegate_task_record_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));

    int ok = delegate_task_store_start("dt_session_ids",
                                       "dc_session_ids",
                                       "delegate_sync_session_ids",
                                       "explore",
                                       "session-ids-task",
                                       "session ids task",
                                       "prompt",
                                       "deepseek-v4-pro",
                                       "kernel/tooling",
                                       "subsystem",
                                       "coordination",
                                       NULL) == 0;
    if (ok) {
        ok = delegate_task_store_append_session_step("dt_session_ids",
                                                     "tool",
                                                     "inspect delegate_parent_wake",
                                                     "delegate_parent_wake.c") == 0;
    }
    if (ok) {
        ok = delegate_task_store_complete("dt_session_ids",
                                          "session ids summary",
                                          "",
                                          false) == 0;
    }
    if (ok) {
        ok = delegate_task_store_snapshot("dt_session_ids", &snapshot) == 0;
    }
    if (ok) {
        delegate_child_session_json_options_t options = {
            .history_limit = 8,
        };
        cJSON *child = delegate_child_session_json_build_from_task(&snapshot, &options);
        cJSON *frames = child ? cJSON_GetObjectItemCaseSensitive(child, "frames") : NULL;
        cJSON *commits = child ? cJSON_GetObjectItemCaseSensitive(child, "commits") : NULL;
        cJSON *first_frame = frames && cJSON_IsArray(frames) ? cJSON_GetArrayItem(frames, 0) : NULL;
        cJSON *last_frame = frames && cJSON_IsArray(frames) ? cJSON_GetArrayItem(frames, cJSON_GetArraySize(frames) - 1) : NULL;
        cJSON *first_commit = commits && cJSON_IsArray(commits) ? cJSON_GetArrayItem(commits, 0) : NULL;
        cJSON *last_commit = commits && cJSON_IsArray(commits) ? cJSON_GetArrayItem(commits, cJSON_GetArraySize(commits) - 1) : NULL;
        cJSON *first_frame_id = first_frame ? cJSON_GetObjectItemCaseSensitive(first_frame, "id") : NULL;
        cJSON *last_frame_id = last_frame ? cJSON_GetObjectItemCaseSensitive(last_frame, "id") : NULL;
        cJSON *first_commit_id = first_commit ? cJSON_GetObjectItemCaseSensitive(first_commit, "id") : NULL;
        cJSON *last_commit_id = last_commit ? cJSON_GetObjectItemCaseSensitive(last_commit, "id") : NULL;

        ok = child &&
             frames &&
             commits &&
             cJSON_IsArray(frames) &&
             cJSON_IsArray(commits) &&
             cJSON_GetArraySize(frames) >= 2 &&
             cJSON_GetArraySize(commits) >= 2 &&
             cJSON_IsString(first_frame_id) &&
             first_frame_id->valuestring[0] &&
             cJSON_IsString(last_frame_id) &&
             last_frame_id->valuestring[0] &&
             strcmp(first_frame_id->valuestring, last_frame_id->valuestring) != 0 &&
             cJSON_IsString(first_commit_id) &&
             first_commit_id->valuestring[0] &&
             cJSON_IsString(last_commit_id) &&
             last_commit_id->valuestring[0] &&
             strcmp(first_commit_id->valuestring, last_commit_id->valuestring) != 0;

        if (!ok) {
            pr_info("  session_ids diag: frame_count=%d commit_count=%d first_frame_id=%s last_frame_id=%s first_commit_id=%s last_commit_id=%s",
                    frames && cJSON_IsArray(frames) ? cJSON_GetArraySize(frames) : -1,
                    commits && cJSON_IsArray(commits) ? cJSON_GetArraySize(commits) : -1,
                    cJSON_IsString(first_frame_id) ? first_frame_id->valuestring : "",
                    cJSON_IsString(last_frame_id) ? last_frame_id->valuestring : "",
                    cJSON_IsString(first_commit_id) ? first_commit_id->valuestring : "",
                    cJSON_IsString(last_commit_id) ? last_commit_id->valuestring : "");
        }
        cJSON_Delete(child);
    }

    report("delegate child session json frames and commits expose ids", ok);
}

static void test_delegate_child_session_json_frames_and_commits_expose_seq(void)
{
    delegate_task_store_reset_for_test();

    delegate_task_record_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));

    int ok = delegate_task_store_start("dt_session_seq",
                                       "dc_session_seq",
                                       "delegate_sync_session_seq",
                                       "explore",
                                       "session seq task",
                                       "session seq task",
                                       "prompt",
                                       "deepseek-v4-pro",
                                       "kernel/tooling",
                                       "subsystem",
                                       "coordination",
                                       NULL) == 0;
    if (ok) {
        ok = delegate_task_store_append_session_step("dt_session_seq",
                                                     "tool",
                                                     "inspect delegate_parent_wake",
                                                     "delegate_parent_wake.c") == 0;
    }
    if (ok) {
        ok = delegate_task_store_complete("dt_session_seq",
                                          "session seq summary",
                                          "",
                                          false) == 0;
    }
    if (ok) {
        ok = delegate_task_store_snapshot("dt_session_seq", &snapshot) == 0;
    }
    if (ok) {
        delegate_child_session_json_options_t options = {
            .history_limit = 8,
        };
        cJSON *child = delegate_child_session_json_build_from_task(&snapshot, &options);
        cJSON *frames = child ? cJSON_GetObjectItemCaseSensitive(child, "frames") : NULL;
        cJSON *commits = child ? cJSON_GetObjectItemCaseSensitive(child, "commits") : NULL;
        cJSON *latest = child ? cJSON_GetObjectItemCaseSensitive(child, "latest_frame") : NULL;
        cJSON *first_frame = frames && cJSON_IsArray(frames) ? cJSON_GetArrayItem(frames, 0) : NULL;
        cJSON *last_frame = frames && cJSON_IsArray(frames) ? cJSON_GetArrayItem(frames, cJSON_GetArraySize(frames) - 1) : NULL;
        cJSON *first_commit = commits && cJSON_IsArray(commits) ? cJSON_GetArrayItem(commits, 0) : NULL;
        cJSON *last_commit = commits && cJSON_IsArray(commits) ? cJSON_GetArrayItem(commits, cJSON_GetArraySize(commits) - 1) : NULL;
        cJSON *first_frame_seq = first_frame ? cJSON_GetObjectItemCaseSensitive(first_frame, "seq") : NULL;
        cJSON *last_frame_seq = last_frame ? cJSON_GetObjectItemCaseSensitive(last_frame, "seq") : NULL;
        cJSON *first_commit_seq = first_commit ? cJSON_GetObjectItemCaseSensitive(first_commit, "seq") : NULL;
        cJSON *last_commit_seq = last_commit ? cJSON_GetObjectItemCaseSensitive(last_commit, "seq") : NULL;
        cJSON *latest_seq = latest ? cJSON_GetObjectItemCaseSensitive(latest, "seq") : NULL;

        ok = child &&
             frames &&
             commits &&
             latest &&
             cJSON_IsArray(frames) &&
             cJSON_IsArray(commits) &&
             cJSON_GetArraySize(frames) >= 2 &&
             cJSON_GetArraySize(commits) >= 2 &&
             cJSON_IsNumber(first_frame_seq) &&
             cJSON_IsNumber(last_frame_seq) &&
             cJSON_IsNumber(first_commit_seq) &&
             cJSON_IsNumber(last_commit_seq) &&
             cJSON_IsNumber(latest_seq) &&
             first_frame_seq->valuedouble > 0 &&
             last_frame_seq->valuedouble > first_frame_seq->valuedouble &&
             first_commit_seq->valuedouble > 0 &&
             last_commit_seq->valuedouble > first_commit_seq->valuedouble &&
             (long long)latest_seq->valuedouble == (long long)last_frame_seq->valuedouble;

        if (!ok) {
            pr_info("  session_seq diag: frame_count=%d commit_count=%d first_frame_seq=%lld last_frame_seq=%lld first_commit_seq=%lld last_commit_seq=%lld latest_seq=%lld",
                    frames && cJSON_IsArray(frames) ? cJSON_GetArraySize(frames) : -1,
                    commits && cJSON_IsArray(commits) ? cJSON_GetArraySize(commits) : -1,
                    cJSON_IsNumber(first_frame_seq) ? (long long)first_frame_seq->valuedouble : -1LL,
                    cJSON_IsNumber(last_frame_seq) ? (long long)last_frame_seq->valuedouble : -1LL,
                    cJSON_IsNumber(first_commit_seq) ? (long long)first_commit_seq->valuedouble : -1LL,
                    cJSON_IsNumber(last_commit_seq) ? (long long)last_commit_seq->valuedouble : -1LL,
                    cJSON_IsNumber(latest_seq) ? (long long)latest_seq->valuedouble : -1LL);
        }
        cJSON_Delete(child);
    }

    report("delegate child session json frames and commits expose seq", ok);
}

static void test_delegate_child_session_json_renders_result_json_visible_text(void)
{
    delegate_task_store_reset_for_test();
    session_store_clear("delegate_sync_child_render");

    delegate_task_record_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));

    int ok = delegate_task_store_start("dt_child_render",
                                       "dc_child_render",
                                       "delegate_sync_child_render",
                                       "oracle",
                                       "child-render-task",
                                       "child render task",
                                       "prompt",
                                       "deepseek-v4-pro",
                                       "kernel/tooling",
                                       "subsystem",
                                       "coordination",
                                       NULL) == 0;
    if (ok) {
        ok = session_store_append("delegate_sync_child_render",
                                  "assistant",
                                  "{\"text\":\"{\\\"status\\\":\\\"done\\\",\\\"summary\\\":\\\"职责边界：kernel/turn 编排回合，kernel/tooling 管理协调。\\\",\\\"evidence\\\":[\\\"kernel/turn/turn_entry.c\\\"],\\\"risks\\\":[],\\\"next_files\\\":[\\\"kernel/tooling/delegate/delegate_parent_wake.c\\\"]}\",\"reasoning\":\"dependency merge shortcut\"}") == 0;
    }
    if (ok) {
        ok = delegate_task_store_complete("dt_child_render",
                                          "{\"status\":\"done\",\"summary\":\"职责边界：kernel/turn 编排回合，kernel/tooling 管理协调。\",\"evidence\":[\"kernel/turn/turn_entry.c\"],\"risks\":[],\"next_files\":[\"kernel/tooling/delegate/delegate_parent_wake.c\"]}",
                                          "",
                                          false) == 0;
    }
    if (ok) {
        ok = delegate_task_store_snapshot("dt_child_render", &snapshot) == 0;
    }
    if (ok) {
        delegate_child_session_json_options_t options = {
            .history_limit = 8,
        };
        cJSON *child = delegate_child_session_json_build_from_task(&snapshot, &options);
        cJSON *summary = child ? cJSON_GetObjectItemCaseSensitive(child, "summary") : NULL;
        cJSON *history = child ? cJSON_GetObjectItemCaseSensitive(child, "history") : NULL;
        cJSON *assistant = history && cJSON_IsArray(history) ? cJSON_GetArrayItem(history, 0) : NULL;
        cJSON *assistant_content = assistant ? cJSON_GetObjectItemCaseSensitive(assistant, "content") : NULL;
        cJSON *commits = child ? cJSON_GetObjectItemCaseSensitive(child, "commits") : NULL;
        cJSON *last_commit = commits && cJSON_IsArray(commits) ? cJSON_GetArrayItem(commits, cJSON_GetArraySize(commits) - 1) : NULL;
        cJSON *last_commit_text = last_commit ? cJSON_GetObjectItemCaseSensitive(last_commit, "text") : NULL;

        ok = child &&
             cJSON_IsString(summary) &&
             strstr(summary->valuestring, "职责边界：kernel/turn 编排回合") != NULL &&
             strstr(summary->valuestring, "{\"status\":\"done\"") == NULL &&
             cJSON_IsString(assistant_content) &&
             strstr(assistant_content->valuestring, "职责边界：kernel/turn 编排回合") != NULL &&
             strstr(assistant_content->valuestring, "{\"status\":\"done\"") == NULL &&
             cJSON_IsString(last_commit_text) &&
             strstr(last_commit_text->valuestring, "职责边界：kernel/turn 编排回合") != NULL &&
             strstr(last_commit_text->valuestring, "{\"status\":\"done\"") == NULL;

        if (!ok) {
            pr_info("  child_render diag: summary=%s assistant=%s commit=%s",
                    cJSON_IsString(summary) ? summary->valuestring : "",
                    cJSON_IsString(assistant_content) ? assistant_content->valuestring : "",
                    cJSON_IsString(last_commit_text) ? last_commit_text->valuestring : "");
        }
        cJSON_Delete(child);
    }

    report("delegate child session json renders result json visible text", ok);
}

static void test_delegate_child_session_preferred_visible_text_prefers_latest_frame_over_stale_history(void)
{
    delegate_task_store_reset_for_test();
    session_store_clear("delegate_sync_child_visible_latest");

    delegate_task_record_t snapshot;
    char preferred[1024];
    memset(&snapshot, 0, sizeof(snapshot));
    memset(preferred, 0, sizeof(preferred));

    int ok = delegate_task_store_start("dt_child_visible_latest",
                                       "dc_child_visible_latest",
                                       "delegate_sync_child_visible_latest",
                                       "explore",
                                       "child-visible-latest",
                                       "child visible latest",
                                       "prompt",
                                       "deepseek-v4-pro",
                                       "kernel/tooling",
                                       "subsystem",
                                       "coordination",
                                       NULL) == 0;
    if (ok) {
        ok = session_store_append("delegate_sync_child_visible_latest",
                                  "assistant",
                                  "{\"text\":\"旧 assistant 文本：这是过时结论。\",\"reasoning\":\"old reasoning\"}") == 0;
    }
    if (ok) {
        ok = delegate_task_store_complete("dt_child_visible_latest",
                                          "{\"status\":\"done\",\"summary\":\"最新结论：kernel/tooling 负责 delegate store、projection 与 parent wake。\",\"evidence\":[\"kernel/tooling/delegate/delegate_task_store.c\",\"kernel/tooling/delegate/delegate_parent_wake.c\"],\"risks\":[],\"next_files\":[\"kernel/tooling/delegate/delegate_state_json.c\"]}",
                                          "",
                                          false) == 0;
    }
    if (ok) {
        ok = delegate_task_store_snapshot("dt_child_visible_latest", &snapshot) == 0;
    }
    if (ok) {
        ok = delegate_child_session_preferred_visible_text(&snapshot,
                                                           preferred,
                                                           sizeof(preferred)) &&
             strstr(preferred, "最新结论：kernel/tooling 负责 delegate store、projection 与 parent wake。") != NULL &&
             strstr(preferred, "旧 assistant 文本：这是过时结论。") == NULL;
    }

    if (!ok) {
        pr_info("  child_visible_latest diag: %s", preferred);
    }
    report("delegate child session preferred visible text prefers latest frame over stale history", ok);
}

static void test_delegate_turn_session_persists_full_child_transcript(void)
{
    session_store_clear("delegate_sync_turn_persist");

    tool_delegate_persist_turn_session("delegate_sync_turn_persist",
                                       "请分析 kernel/tooling 和 parent wake 的职责边界",
                                       "child assistant final summary about parent wake",
                                       "child reasoning about coordinator lifecycle");

    char history_json[8192];
    history_json[0] = '\0';
    int ok = session_store_get_history_json("delegate_sync_turn_persist",
                                            history_json,
                                            sizeof(history_json),
                                            8) == 0;
    cJSON *root = ok ? cJSON_Parse(history_json) : NULL;
    cJSON *first = root && cJSON_IsArray(root) ? cJSON_GetArrayItem(root, 0) : NULL;
    cJSON *second = root && cJSON_IsArray(root) ? cJSON_GetArrayItem(root, 1) : NULL;
    cJSON *first_role = first ? cJSON_GetObjectItemCaseSensitive(first, "role") : NULL;
    cJSON *first_content = first ? cJSON_GetObjectItemCaseSensitive(first, "content") : NULL;
    cJSON *second_role = second ? cJSON_GetObjectItemCaseSensitive(second, "role") : NULL;
    cJSON *second_content = second ? cJSON_GetObjectItemCaseSensitive(second, "content") : NULL;
    cJSON *assistant_payload = cJSON_IsString(second_content) ? cJSON_Parse(second_content->valuestring) : NULL;
    cJSON *assistant_text = assistant_payload ? cJSON_GetObjectItemCaseSensitive(assistant_payload, "text") : NULL;
    cJSON *assistant_reasoning = assistant_payload ? cJSON_GetObjectItemCaseSensitive(assistant_payload, "reasoning") : NULL;

    ok = ok &&
         root &&
         cJSON_IsArray(root) &&
         cJSON_GetArraySize(root) >= 2 &&
         cJSON_IsString(first_role) &&
         strcmp(first_role->valuestring, "user") == 0 &&
         cJSON_IsString(first_content) &&
         strstr(first_content->valuestring, "请分析 kernel/tooling") != NULL &&
         cJSON_IsString(second_role) &&
         strcmp(second_role->valuestring, "assistant") == 0 &&
         cJSON_IsString(assistant_text) &&
         strstr(assistant_text->valuestring, "child assistant final summary") != NULL &&
         cJSON_IsString(assistant_reasoning) &&
         strstr(assistant_reasoning->valuestring, "child reasoning about coordinator lifecycle") != NULL;

    if (!ok) {
        pr_info("  turn_session_persist diag: history=%s", history_json);
    }
    cJSON_Delete(assistant_payload);
    cJSON_Delete(root);

    report("delegate turn session persists full child transcript", ok);
}

static void test_delegate_child_session_json_does_not_leak_reused_session_history(void)
{
    delegate_task_store_reset_for_test();
    session_store_clear("delegate_sync_reused_history");

    int ok = session_store_append("delegate_sync_reused_history",
                                  "user",
                                  "stale history from previous child run") == 0;
    ok = ok && session_store_append("delegate_sync_reused_history",
                                    "assistant",
                                    "{\"text\":\"stale assistant output\",\"reasoning\":\"stale reasoning\"}") == 0;

    if (ok) {
        ok = delegate_task_store_start("dt_reused_history",
                                       "dc_reused_history",
                                       "delegate_sync_reused_history",
                                       "explore",
                                       "reused-history-task",
                                       "reused history task",
                                       "prompt",
                                       "deepseek-v4-pro",
                                       "/home/wangergou/code/github/daima-agent/kernel/turn",
                                       "subsystem",
                                       "turn_execution",
                                       NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_append_session_step("dt_reused_history",
                                                     "tool",
                                                     "fresh tool step",
                                                     "fresh output") == 0;
    }
    if (ok) {
        ok = delegate_task_store_complete("dt_reused_history",
                                          "fresh result summary",
                                          "",
                                          false) == 0;
    }
    if (ok) {
        delegate_task_record_t snapshot;
        memset(&snapshot, 0, sizeof(snapshot));
        ok = delegate_task_store_snapshot("dt_reused_history", &snapshot) == 0;
        if (ok) {
            delegate_child_session_json_options_t options = {
                .history_limit = 8,
            };
            cJSON *child = delegate_child_session_json_build_from_task(&snapshot, &options);
            cJSON *history = child ? cJSON_GetObjectItemCaseSensitive(child, "history") : NULL;
            char *json = child ? cJSON_PrintUnformatted(child) : NULL;
            ok = child &&
                 history &&
                 cJSON_IsArray(history) &&
                 cJSON_GetArraySize(history) == 0 &&
                 json &&
                 strstr(json, "stale history from previous child run") == NULL &&
                 strstr(json, "stale assistant output") == NULL;
            if (!ok) {
                pr_info("  reused_session_history diag: child=%s",
                        json ? json : "<null>");
            }
            if (json) {
                free(json);
            }
            cJSON_Delete(child);
        }
    }

    report("delegate child session json does not leak reused session history", ok);
}

static void test_delegate_background_local_overview_shortcut_records_child_session_step(void)
{
    delegate_task_store_reset_for_test();
    char output[8192];
    char poll_output[8192];
    char task_id[DELEGATE_TASK_ID_LEN] = {0};
    delegate_task_record_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    memset(output, 0, sizeof(output));
    memset(poll_output, 0, sizeof(poll_output));

    const char *input =
        "{"
        "\"subagent_type\":\"explore\","
        "\"description\":\"分析 kernel/turn 目录结构\","
        "\"prompt\":\"分析 /home/wangergou/code/github/daima-agent/kernel/turn 的目录结构和关键模块。只做代表性覆盖，不要穷举。\","
        "\"run_in_background\":true"
        "}";

    const struct tool *t = tool_delegate_definition();
    err_t err = t->execute(input, output, sizeof(output));
    int ok = (err == 0) &&
             strstr(output, "\"task_id\":\"dt_") &&
             strstr(output, "\"status\":\"running\"");

    if (ok) {
        const char *marker = strstr(output, "\"task_id\":\"");
        if (marker) {
            marker += strlen("\"task_id\":\"");
            int i = 0;
            while (marker[i] && marker[i] != '"' && i < (int)sizeof(task_id) - 1) {
                task_id[i] = marker[i];
                i++;
            }
            task_id[i] = '\0';
        }
        ok = task_id[0] != '\0';
    }

    if (ok) {
        for (int i = 0; i < 40; i++) {
            snprintf(poll_output, sizeof(poll_output), "{\"task_id\":\"%s\"}", task_id);
            err = t->execute(poll_output, poll_output, sizeof(poll_output));
            if (err == 0 && strstr(poll_output, "\"status\":\"done\"")) {
                break;
            }
            usleep(100000);
        }
        ok = err == 0 && strstr(poll_output, "\"status\":\"done\"");
    }

    if (ok) {
        ok = delegate_task_store_snapshot(task_id, &snapshot) == 0 &&
             snapshot.child_session.frame_count >= 3 &&
             strcmp(snapshot.child_session.frames[snapshot.child_session.frame_count - 2].type, "subagent_step") == 0 &&
             strcmp(snapshot.child_session.frames[snapshot.child_session.frame_count - 2].blocker_kind, "tool") == 0 &&
             strstr(snapshot.child_session.frames[snapshot.child_session.frame_count - 2].detail, "local repo overview shortcut") != NULL &&
             strcmp(snapshot.child_session.frames[snapshot.child_session.frame_count - 1].type, "subagent_done") == 0 &&
             snapshot.child_session.commit_count >= 3 &&
             strcmp(snapshot.child_session.commits[snapshot.child_session.commit_count - 2].kind, "tool") == 0 &&
             strstr(snapshot.child_session.commits[snapshot.child_session.commit_count - 2].text, "local repo overview shortcut") != NULL &&
             strcmp(snapshot.child_session.commits[snapshot.child_session.commit_count - 1].kind, "result") == 0;
    }
    if (!ok) {
        pr_info("  background_local_overview_step diag: task_id=%s poll=%s frame_count=%d commit_count=%d prev_frame=%s prev_commit=%s",
                task_id,
                poll_output,
                snapshot.child_session.frame_count,
                snapshot.child_session.commit_count,
                snapshot.child_session.frame_count >= 2
                    ? snapshot.child_session.frames[snapshot.child_session.frame_count - 2].type
                    : "",
                snapshot.child_session.commit_count >= 2
                    ? snapshot.child_session.commits[snapshot.child_session.commit_count - 2].kind
                    : "");
    }

    report("delegate background local overview shortcut records child session step", ok);
}

static void test_delegate_background_dependency_merge_shortcut_records_child_session_step(void)
{
    delegate_task_store_reset_for_test();
    char output[8192];
    char poll_output[8192];
    char coordinator_id[DELEGATE_COORDINATOR_ID_LEN] = {0};
    char second_task_id[DELEGATE_TASK_ID_LEN] = {0};
    delegate_coordinator_record_t coordinator_snapshot;
    delegate_task_record_t second_snapshot;
    memset(&coordinator_snapshot, 0, sizeof(coordinator_snapshot));
    memset(&second_snapshot, 0, sizeof(second_snapshot));
    memset(output, 0, sizeof(output));
    memset(poll_output, 0, sizeof(poll_output));

    const char *input =
        "{"
        "\"tasks\":["
          "{"
            "\"task_key\":\"map-kernel\","
            "\"description\":\"分析 kernel\","
            "\"prompt\":\"分析 /home/wangergou/code/github/daima-agent/kernel 的目录结构和关键模块，说明入口与主链。\","
            "\"subagent_type\":\"explore\""
          "},"
          "{"
            "\"task_key\":\"merge-kernel\","
            "\"description\":\"汇总 kernel 发现\","
            "\"prompt\":\"结合上游结果，总结 kernel 相关发现。\","
            "\"depends_on\":\"map-kernel\","
            "\"subagent_type\":\"oracle\""
          "}"
        "]"
        "}";

    const struct tool *t = tool_delegate_definition();
    err_t err = t->execute(input, output, sizeof(output));
    int ok = (err == 0) &&
             strstr(output, "\"coordinator_id\":\"dc_") &&
             strstr(output, "\"task_key\":\"map-kernel\"") &&
             strstr(output, "\"task_key\":\"merge-kernel\"");

    if (ok) {
        const char *marker = strstr(output, "\"coordinator_id\":\"");
        if (marker) {
            marker += strlen("\"coordinator_id\":\"");
            int i = 0;
            while (marker[i] && marker[i] != '"' && i < (int)sizeof(coordinator_id) - 1) {
                coordinator_id[i] = marker[i];
                i++;
            }
            coordinator_id[i] = '\0';
        }
        ok = coordinator_id[0] != '\0';
    }

    if (ok) {
        for (int i = 0; i < 60; i++) {
            agent_loop_poll_delegate_coordinators();
            snprintf(poll_output, sizeof(poll_output), "{\"coordinator_id\":\"%s\"}", coordinator_id);
            err = t->execute(poll_output, poll_output, sizeof(poll_output));
            if (err == 0 && strstr(poll_output, "\"status\":\"done\"")) {
                break;
            }
            usleep(100000);
        }
        ok = err == 0 && strstr(poll_output, "\"status\":\"done\"");
    }

    if (ok) {
        for (int i = 0; i < 60; i++) {
            agent_loop_poll_delegate_coordinators();
            memset(&coordinator_snapshot, 0, sizeof(coordinator_snapshot));
            if (delegate_task_store_snapshot_coordinator(coordinator_id, &coordinator_snapshot) == 0) {
                for (int j = 0; j < coordinator_snapshot.agent_count; j++) {
                    if (strcmp(coordinator_snapshot.agents[j].task_key, "merge-kernel") == 0) {
                        strscpy(second_task_id,
                                coordinator_snapshot.agents[j].task_id,
                                sizeof(second_task_id));
                        break;
                    }
                }
            }
            if (second_task_id[0]) {
                break;
            }
            usleep(100000);
        }
        ok = second_task_id[0] != '\0';
    }

    if (ok) {
        for (int i = 0; i < 60; i++) {
            agent_loop_poll_delegate_coordinators();
            memset(&second_snapshot, 0, sizeof(second_snapshot));
            if (delegate_task_store_snapshot(second_task_id, &second_snapshot) == 0 &&
                second_snapshot.status == DELEGATE_TASK_DONE) {
                break;
            }
            usleep(100000);
        }
    }

    if (ok) {
        ok = delegate_task_store_snapshot(second_task_id, &second_snapshot) == 0 &&
             second_snapshot.status == DELEGATE_TASK_DONE &&
             second_snapshot.child_session.frame_count >= 3 &&
             strcmp(second_snapshot.child_session.frames[second_snapshot.child_session.frame_count - 2].type, "subagent_step") == 0 &&
             strcmp(second_snapshot.child_session.frames[second_snapshot.child_session.frame_count - 2].blocker_kind, "tool") == 0 &&
             strstr(second_snapshot.child_session.frames[second_snapshot.child_session.frame_count - 2].detail, "dependency merge shortcut") != NULL &&
             strcmp(second_snapshot.child_session.frames[second_snapshot.child_session.frame_count - 1].type, "subagent_done") == 0 &&
             second_snapshot.child_session.commit_count >= 3 &&
             strcmp(second_snapshot.child_session.commits[second_snapshot.child_session.commit_count - 2].kind, "tool") == 0 &&
             strstr(second_snapshot.child_session.commits[second_snapshot.child_session.commit_count - 2].text, "dependency merge shortcut") != NULL &&
             strcmp(second_snapshot.child_session.commits[second_snapshot.child_session.commit_count - 1].kind, "result") == 0;
    }
    if (!ok) {
        pr_info("  background_dependency_merge_step diag: coordinator=%s second=%s poll=%s status=%d frame_count=%d commit_count=%d prev_frame=%s prev_commit=%s",
                coordinator_id,
                second_task_id,
                poll_output,
                second_snapshot.status,
                second_snapshot.child_session.frame_count,
                second_snapshot.child_session.commit_count,
                second_snapshot.child_session.frame_count >= 2
                    ? second_snapshot.child_session.frames[second_snapshot.child_session.frame_count - 2].type
                    : "",
                second_snapshot.child_session.commit_count >= 2
                    ? second_snapshot.child_session.commits[second_snapshot.child_session.commit_count - 2].kind
                    : "");
    }

    report("delegate background dependency merge shortcut records child session step", ok);
}

typedef struct {
    int status_calls;
    int output_calls;
    int done_calls;
    int subagent_calls;
    int fail_done_once;
    int fail_status_once;
    char last_chat_id[64];
    char last_status_payload[16384];
    char last_output_payload[16384];
    char last_done_payload[16384];
    char last_subagent_status[32];
    char last_subagent_task_id[32];
    char last_subagent_detail[512];
    char last_subagent_output[1024];
    char last_subagent_visible_output[1024];
    char last_nonempty_subagent_detail[512];
    char last_event_type[32];
} delegate_wake_test_state_t;

static delegate_wake_test_state_t s_delegate_wake_test_state;

typedef struct {
    int interactive_calls;
    int sudo_calls;
    char last_chat_id[64];
    char last_request_type[32];
    char last_request_id[64];
    char last_prompt[256];
    char last_task_id[32];
    char last_session_id[32];
    char last_coordinator_id[32];
} interactive_test_state_t;

static interactive_test_state_t s_interactive_test_state;

static void drain_inbound_bus_for_test(void)
{
    struct message msg;

    while (1) {
        memset(&msg, 0, sizeof(msg));
        if (message_bus_pop_inbound(&msg, 0) != 0) {
            break;
        }
        free(msg.content);
        free(msg.reasoning);
        free(msg.image_path);
    }
}

static void drain_outbound_bus_for_test(void)
{
    struct message msg;

    while (1) {
        memset(&msg, 0, sizeof(msg));
        if (message_bus_pop_outbound(&msg, 0) != 0) {
            break;
        }
        free(msg.content);
        free(msg.reasoning);
        free(msg.image_path);
    }
}

static void drain_scheduler_reply_bus_for_test(void)
{
    struct core_task task;

    while (1) {
        memset(&task, 0, sizeof(task));
        if (core_recv(CORE_SCHEDULER, &task, 0) != 0) {
            break;
        }
        kfree(task.payload);
        kfree(task.result);
    }
}

static void reset_interactive_test_state(void)
{
    memset(&s_interactive_test_state, 0, sizeof(s_interactive_test_state));
    interactive_set_sender_overrides_for_test(NULL, NULL);
}

static err_t test_interactive_sender(const char *chat_id,
                                     const char *request_type,
                                     const char *request_id,
                                     const char *prompt_text,
                                     const char *task_id,
                                     const char *session_id,
                                     const char *coordinator_id)
{
    s_interactive_test_state.interactive_calls++;
    strscpy(s_interactive_test_state.last_chat_id, chat_id ? chat_id : "", sizeof(s_interactive_test_state.last_chat_id));
    strscpy(s_interactive_test_state.last_request_type, request_type ? request_type : "", sizeof(s_interactive_test_state.last_request_type));
    strscpy(s_interactive_test_state.last_request_id, request_id ? request_id : "", sizeof(s_interactive_test_state.last_request_id));
    strscpy(s_interactive_test_state.last_prompt, prompt_text ? prompt_text : "", sizeof(s_interactive_test_state.last_prompt));
    strscpy(s_interactive_test_state.last_task_id, task_id ? task_id : "", sizeof(s_interactive_test_state.last_task_id));
    strscpy(s_interactive_test_state.last_session_id, session_id ? session_id : "", sizeof(s_interactive_test_state.last_session_id));
    strscpy(s_interactive_test_state.last_coordinator_id, coordinator_id ? coordinator_id : "", sizeof(s_interactive_test_state.last_coordinator_id));
    return 0;
}

static err_t test_sudo_sender(const char *chat_id,
                              const char *request_id,
                              const char *prompt_text,
                              const char *task_id,
                              const char *session_id,
                              const char *coordinator_id)
{
    s_interactive_test_state.sudo_calls++;
    strscpy(s_interactive_test_state.last_chat_id, chat_id ? chat_id : "", sizeof(s_interactive_test_state.last_chat_id));
    strscpy(s_interactive_test_state.last_request_type, "sudo_password", sizeof(s_interactive_test_state.last_request_type));
    strscpy(s_interactive_test_state.last_request_id, request_id ? request_id : "", sizeof(s_interactive_test_state.last_request_id));
    strscpy(s_interactive_test_state.last_prompt, prompt_text ? prompt_text : "", sizeof(s_interactive_test_state.last_prompt));
    strscpy(s_interactive_test_state.last_task_id, task_id ? task_id : "", sizeof(s_interactive_test_state.last_task_id));
    strscpy(s_interactive_test_state.last_session_id, session_id ? session_id : "", sizeof(s_interactive_test_state.last_session_id));
    strscpy(s_interactive_test_state.last_coordinator_id, coordinator_id ? coordinator_id : "", sizeof(s_interactive_test_state.last_coordinator_id));
    return 0;
}

static void wait_for_delegate_background_idle_for_test(void)
{
    for (int i = 0; i < 120; i++) {
        agent_loop_poll_delegate_coordinators();
        if (delegate_task_store_running_count() == 0) {
            return;
        }
        usleep(50000);
    }
    pr_warn("delegate background tasks still running at self-test teardown");
}

static bool wait_for_delegate_lifecycle_snapshot(bool (*predicate)(void *ctx),
                                                 void *ctx,
                                                 int timeout_ms)
{
    int attempts;

    if (!predicate || timeout_ms <= 0) {
        return false;
    }

    attempts = timeout_ms / 20;
    if (attempts <= 0) {
        attempts = 1;
    }

    for (int i = 0; i < attempts; i++) {
        agent_loop_poll_delegate_coordinators();
        if (predicate(ctx)) {
            return true;
        }
        usleep(20000);
    }

    return predicate(ctx);
}

static bool predicate_delegate_fair_launch(void *ctx)
{
    delegate_two_snapshot_wait_t *state = (delegate_two_snapshot_wait_t *)ctx;

    return delegate_task_store_snapshot_coordinator("dc_fair_a", state->left) == 0 &&
           delegate_task_store_snapshot_coordinator("dc_fair_b", state->right) == 0 &&
           state->left->running_count == 1 &&
           state->right->running_count == 1 &&
           state->left->queued_count == 1 &&
           state->right->queued_count == 1;
}

static bool predicate_delegate_per_coordinator_cap(void *ctx)
{
    delegate_two_snapshot_wait_t *state = (delegate_two_snapshot_wait_t *)ctx;

    return delegate_task_store_snapshot_coordinator("dc_cap_heavy", state->left) == 0 &&
           delegate_task_store_snapshot_coordinator("dc_cap_light", state->right) == 0 &&
           state->left->running_count == 1 &&
           state->left->queued_count == 2 &&
           state->right->running_count == 1 &&
           state->right->queued_count == 0;
}

static bool predicate_delegate_per_parent_cap(void *ctx)
{
    delegate_three_snapshot_wait_t *state = (delegate_three_snapshot_wait_t *)ctx;

    return delegate_task_store_snapshot_coordinator("dc_parent_a1", state->left) == 0 &&
           delegate_task_store_snapshot_coordinator("dc_parent_a2", state->middle) == 0 &&
           delegate_task_store_snapshot_coordinator("dc_parent_b1", state->right) == 0 &&
           state->left->running_count == 1 &&
           state->middle->running_count == 1 &&
           state->right->running_count == 1 &&
           state->left->queued_count == 1 &&
           state->middle->queued_count == 1 &&
           state->right->queued_count == 0;
}

static bool predicate_delegate_blocked_coordinator_cap(void *ctx)
{
    delegate_coordinator_record_t *snapshot = (delegate_coordinator_record_t *)ctx;

    return delegate_task_store_snapshot_coordinator("dc_block_cap", snapshot) == 0 &&
           snapshot->running_count == 1 &&
           snapshot->blocked_count == 1 &&
           snapshot->queued_count == 0;
}

static bool predicate_delegate_blocked_parent_cap(void *ctx)
{
    delegate_two_snapshot_wait_t *state = (delegate_two_snapshot_wait_t *)ctx;

    return delegate_task_store_snapshot_coordinator("dc_parent_blocked", state->left) == 0 &&
           delegate_task_store_snapshot_coordinator("dc_parent_ready", state->right) == 0 &&
           state->left->blocked_count == 1 &&
           state->left->running_count == 0 &&
           state->right->running_count == 1 &&
           state->right->queued_count == 0;
}

static bool predicate_delegate_long_prompt_schedulable(void *ctx)
{
    delegate_single_snapshot_wait_t *state = (delegate_single_snapshot_wait_t *)ctx;
    delegate_coordinator_record_t *snapshot;
    int active_or_done = 0;
    int aggregate_active_or_done = 0;

    if (!state || !state->coordinator_id[0] || !state->snapshot) {
        return false;
    }

    snapshot = state->snapshot;
    if (delegate_task_store_snapshot_coordinator(state->coordinator_id, snapshot) != 0) {
        return false;
    }

    for (int i = 0; i < snapshot->agent_count; i++) {
        const char *status = snapshot->agents[i].status;
        if (strcmp(status, "running") == 0 ||
            strcmp(status, "done") == 0 ||
            strcmp(status, "error") == 0 ||
            strcmp(status, "failed") == 0) {
            active_or_done++;
        }
    }

    aggregate_active_or_done = snapshot->running_count +
                               snapshot->completed_count +
                               snapshot->failed_count;

    if (snapshot->agent_count >= 3 &&
        (active_or_done >= 3 || aggregate_active_or_done >= 3)) {
        return true;
    }

    return snapshot->queued_count == 0 && strcmp(snapshot->status, "done") == 0;
}

static err_t test_status_sender(const char *chat_id, const char *payload)
{
    s_delegate_wake_test_state.status_calls++;
    strscpy(s_delegate_wake_test_state.last_chat_id, chat_id ? chat_id : "",
            sizeof(s_delegate_wake_test_state.last_chat_id));
    strscpy(s_delegate_wake_test_state.last_status_payload, payload ? payload : "",
            sizeof(s_delegate_wake_test_state.last_status_payload));
    if (s_delegate_wake_test_state.fail_status_once > 0) {
        s_delegate_wake_test_state.fail_status_once--;
        return ERR_NOT_FOUND;
    }
    return 0;
}

static err_t test_output_sender(const char *chat_id, const char *payload)
{
    s_delegate_wake_test_state.output_calls++;
    strscpy(s_delegate_wake_test_state.last_chat_id, chat_id ? chat_id : "",
            sizeof(s_delegate_wake_test_state.last_chat_id));
    strscpy(s_delegate_wake_test_state.last_output_payload, payload ? payload : "",
            sizeof(s_delegate_wake_test_state.last_output_payload));
    return 0;
}

static err_t test_done_sender(const char *chat_id, const char *payload)
{
    s_delegate_wake_test_state.done_calls++;
    strscpy(s_delegate_wake_test_state.last_chat_id, chat_id ? chat_id : "",
            sizeof(s_delegate_wake_test_state.last_chat_id));
    strscpy(s_delegate_wake_test_state.last_done_payload, payload ? payload : "",
            sizeof(s_delegate_wake_test_state.last_done_payload));
    if (s_delegate_wake_test_state.fail_done_once > 0) {
        s_delegate_wake_test_state.fail_done_once--;
        return ERR_FAIL;
    }
    return 0;
}

static err_t test_subagent_sender(const char *chat_id,
                                  const char *event_type,
                                  const char *task_id,
                                  const char *session_id,
                                  const char *coordinator_id,
                                  unsigned long visible_revision,
                                  const char *subagent_type,
                                  const char *status,
                                  const char *task,
                                  const char *detail,
                                  const char *output,
                                  const char *visible_output,
                                  const char *target_files,
                                  const char *scope_path,
                                  const char *scope_kind,
                                  const char *analysis_focus,
                                  const char *blocker_kind,
                                  const char *blocker_text,
                                  const char *blocker_scope)
{
    (void)session_id;
    (void)coordinator_id;
    (void)visible_revision;
    (void)subagent_type;
    (void)task;
    (void)output;
    (void)target_files;
    (void)scope_path;
    (void)scope_kind;
    (void)analysis_focus;
    (void)blocker_kind;
    (void)blocker_text;
    (void)blocker_scope;
    s_delegate_wake_test_state.subagent_calls++;
    strscpy(s_delegate_wake_test_state.last_event_type, event_type ? event_type : "",
            sizeof(s_delegate_wake_test_state.last_event_type));
    strscpy(s_delegate_wake_test_state.last_chat_id, chat_id ? chat_id : "",
            sizeof(s_delegate_wake_test_state.last_chat_id));
    strscpy(s_delegate_wake_test_state.last_subagent_status, status ? status : "",
            sizeof(s_delegate_wake_test_state.last_subagent_status));
    strscpy(s_delegate_wake_test_state.last_subagent_task_id, task_id ? task_id : "",
            sizeof(s_delegate_wake_test_state.last_subagent_task_id));
    strscpy(s_delegate_wake_test_state.last_subagent_detail, detail ? detail : "",
            sizeof(s_delegate_wake_test_state.last_subagent_detail));
    strscpy(s_delegate_wake_test_state.last_subagent_output, output ? output : "",
            sizeof(s_delegate_wake_test_state.last_subagent_output));
    strscpy(s_delegate_wake_test_state.last_subagent_visible_output, visible_output ? visible_output : "",
            sizeof(s_delegate_wake_test_state.last_subagent_visible_output));
    if (detail && detail[0]) {
        strscpy(s_delegate_wake_test_state.last_nonempty_subagent_detail, detail,
                sizeof(s_delegate_wake_test_state.last_nonempty_subagent_detail));
    }
    return 0;
}

static void reset_delegate_wake_test_state(void)
{
    memset(&s_delegate_wake_test_state, 0, sizeof(s_delegate_wake_test_state));
    delegate_parent_wake_reset_for_test();
    delegate_parent_wake_set_sender_overrides_for_test(test_status_sender,
                                                       test_output_sender,
                                                       test_done_sender,
                                                       test_subagent_sender);
}

static void reset_delegate_wake_test_env(void)
{
    delegate_task_store_reset_for_test();
    reset_delegate_wake_test_state();
    channel_runtime_set_sender_override_for_test(NULL);
    drain_inbound_bus_for_test();
    drain_outbound_bus_for_test();
    drain_scheduler_reply_bus_for_test();
}

static err_t test_channel_sender(const char *channel,
                                 const char *chat_id,
                                 const char *text,
                                 const char *reasoning)
{
    (void)channel;
    (void)chat_id;
    (void)text;
    (void)reasoning;
    return 0;
}

static void test_delegate_parent_wake_waits_for_parent_response(void)
{
    reset_delegate_wake_test_env();
    cJSON *status_root = NULL;
    cJSON *status_agents = NULL;
    cJSON *status_agent = NULL;
    cJSON *status_output_root = NULL;

    int ok = delegate_task_store_start_coordinator("dc_wake_wait", "chat_wake_wait", "", "", "parallel") == 0;
    if (ok) {
        ok = delegate_task_store_start("dt_wake_wait", "dc_wake_wait", "delegate_sync_wait", "explore",
                                       "", "wake gating", "prompt", "deepseek-v4-pro",
                                       "kernel/tooling", "subsystem", "coordination", NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_attach_task("dc_wake_wait", "dt_wake_wait") == 0;
    }

    agent_loop_poll_delegate_coordinators();
    if (ok) {
        ok = s_delegate_wake_test_state.status_calls == 0 &&
             s_delegate_wake_test_state.output_calls == 0 &&
             s_delegate_wake_test_state.done_calls == 0 &&
             delegate_parent_wake_pending_count_for_test() == 1;
    }

    if (ok) {
        ok = delegate_task_store_mark_parent_response_sent("chat_wake_wait") == 0;
    }
    if (ok) {
        session_store_clear("delegate_sync_wait");
        session_store_append("delegate_sync_wait", "user", "请先确认是否继续当前子任务");
        session_store_append("delegate_sync_wait", "assistant",
                             "{\"text\":\"可以继续，但需要先确认。\",\"reasoning\":\"waiting for user confirmation\"}");
    }
    if (ok) {
        ok = delegate_task_store_set_pending_request("dt_wake_wait",
                                                     "question_text",
                                                     "question_req_wake_wait_1",
                                                     "请先确认是否继续当前子任务") == 0;
    }
    agent_loop_poll_delegate_coordinators();
    if (ok) {
        status_root = cJSON_Parse(s_delegate_wake_test_state.last_status_payload);
        status_agents = status_root ? cJSON_GetObjectItem(status_root, "agents") : NULL;
        status_agent = status_agents && cJSON_IsArray(status_agents) ? cJSON_GetArrayItem(status_agents, 0) : NULL;
        status_output_root = cJSON_Parse(s_delegate_wake_test_state.last_output_payload);

        ok = s_delegate_wake_test_state.status_calls == 1 &&
             s_delegate_wake_test_state.output_calls == 1 &&
             s_delegate_wake_test_state.subagent_calls >= 1 &&
             status_root &&
             status_agent &&
             status_output_root &&
             strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(status_root, "wake_state")), "dispatched") == 0 &&
             strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(status_root, "coordinator_id")), "dc_wake_wait") == 0 &&
             strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(status_agent, "task_id")), "dt_wake_wait") == 0 &&
             strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(status_agent, "status")), "blocked") == 0 &&
             strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(status_output_root, "coordinator_id")), "dc_wake_wait") == 0;
    }
    if (ok) {
        ok = strcmp(s_delegate_wake_test_state.last_subagent_task_id, "dt_wake_wait") == 0 &&
             strcmp(s_delegate_wake_test_state.last_subagent_status, "blocked") == 0 &&
             strstr(s_delegate_wake_test_state.last_nonempty_subagent_detail, "elapsed_ms=");
    }

    delegate_coordinator_record_t record;
    memset(&record, 0, sizeof(record));
    if (ok) {
        ok = delegate_task_store_snapshot_coordinator("dc_wake_wait", &record) == 0 &&
             record.wake_state == DELEGATE_WAKE_DISPATCHED;
    }
    if (!ok) {
        pr_info("  wake_wait diag: status_calls=%d output_calls=%d done_calls=%d subagent_calls=%d pending=%d",
                s_delegate_wake_test_state.status_calls,
                s_delegate_wake_test_state.output_calls,
                s_delegate_wake_test_state.done_calls,
                s_delegate_wake_test_state.subagent_calls,
                delegate_parent_wake_pending_count_for_test());
        pr_info("  wake_wait status payload: %s", s_delegate_wake_test_state.last_status_payload);
        pr_info("  wake_wait output payload: %s", s_delegate_wake_test_state.last_output_payload);
        pr_info("  wake_wait subagent detail: %s", s_delegate_wake_test_state.last_subagent_detail);
        pr_info("  wake_wait subagent detail(nonempty): %s", s_delegate_wake_test_state.last_nonempty_subagent_detail);
    }

    cJSON_Delete(status_root);
    cJSON_Delete(status_output_root);

    report("delegate parent wake waits for parent response", ok);
}

static void test_sudo_request_routes_delegate_child_to_parent_context(void)
{
    reset_interactive_test_state();
    delegate_task_store_reset_for_test();

    int ok = delegate_task_store_start_coordinator("dc_sudo_parent", "chat_parent_sudo", "", "", "parallel") == 0;
    if (ok) {
        ok = delegate_task_store_start("dt_sudo_parent",
                                       "dc_sudo_parent",
                                       "delegate_sync_sudo_parent",
                                       "explore",
                                       "",
                                       "sudo check",
                                       "prompt",
                                       "deepseek-v4-pro",
                                       "/home/wangergou/code/github/daima-agent",
                                       "subsystem",
                                       "tool_runtime",
                                       NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_attach_task("dc_sudo_parent", "dt_sudo_parent") == 0;
    }

    interactive_set_sender_overrides_for_test(test_interactive_sender, test_sudo_sender);

    struct message msg;
    memset(&msg, 0, sizeof(msg));
    strscpy(msg.channel, CHAN_WEBSOCKET, sizeof(msg.channel));
    strscpy(msg.chat_id, "delegate_sync_sudo_parent", sizeof(msg.chat_id));
    strscpy(msg.source, MSG_SOURCE_INTERNAL, sizeof(msg.source));
    msg.content = strdup("sudo preflight");

    if (ok) {
        ok = channel_runtime_request_sudo(&msg,
                                          "sudo_req_parent_1",
                                          "This command requires sudo privileges.") == 0;
    }
    if (ok) {
        ok = s_interactive_test_state.sudo_calls == 1 &&
             s_interactive_test_state.interactive_calls == 0 &&
             strcmp(s_interactive_test_state.last_chat_id, "chat_parent_sudo") == 0 &&
             strcmp(s_interactive_test_state.last_task_id, "dt_sudo_parent") == 0 &&
             strcmp(s_interactive_test_state.last_session_id, "delegate_sync_sudo_parent") == 0 &&
             strcmp(s_interactive_test_state.last_coordinator_id, "dc_sudo_parent") == 0 &&
             strcmp(s_interactive_test_state.last_request_id, "sudo_req_parent_1") == 0 &&
             strstr(s_interactive_test_state.last_prompt, "sudo");
    }

    free(msg.content);
    reset_interactive_test_state();
    report("sudo request routes delegate child to parent context", ok);
}

static void test_interactive_request_routes_delegate_child_to_parent_context(void)
{
    reset_interactive_test_state();
    delegate_task_store_reset_for_test();

    int ok = delegate_task_store_start_coordinator("dc_question_parent", "chat_parent_question", "", "", "parallel") == 0;
    if (ok) {
        ok = delegate_task_store_start("dt_question_parent",
                                       "dc_question_parent",
                                       "delegate_sync_question_parent",
                                       "explore",
                                       "",
                                       "question check",
                                       "prompt",
                                       "deepseek-v4-pro",
                                       "/home/wangergou/code/github/daima-agent",
                                       "subsystem",
                                       "turn_interview",
                                       NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_attach_task("dc_question_parent", "dt_question_parent") == 0;
    }

    interactive_set_sender_overrides_for_test(test_interactive_sender, test_sudo_sender);

    struct message msg;
    memset(&msg, 0, sizeof(msg));
    strscpy(msg.channel, CHAN_WEBSOCKET, sizeof(msg.channel));
    strscpy(msg.chat_id, "delegate_sync_question_parent", sizeof(msg.chat_id));
    strscpy(msg.source, MSG_SOURCE_INTERNAL, sizeof(msg.source));
    msg.content = strdup("interactive question");

    interactive_request_meta_t meta = {0};
    meta.request_type = "question_text";
    meta.request_id = "question_req_parent_1";
    meta.prompt_text = "请先确认要改哪个模块。";

    if (ok) {
        ok = channel_runtime_request_interactive(&msg, &meta) == 0;
    }
    if (ok) {
        ok = s_interactive_test_state.interactive_calls == 1 &&
             s_interactive_test_state.sudo_calls == 0 &&
             strcmp(s_interactive_test_state.last_chat_id, "chat_parent_question") == 0 &&
             strcmp(s_interactive_test_state.last_request_type, "question_text") == 0 &&
             strcmp(s_interactive_test_state.last_task_id, "dt_question_parent") == 0 &&
             strcmp(s_interactive_test_state.last_session_id, "delegate_sync_question_parent") == 0 &&
             strcmp(s_interactive_test_state.last_coordinator_id, "dc_question_parent") == 0 &&
             strcmp(s_interactive_test_state.last_request_id, "question_req_parent_1") == 0 &&
             strstr(s_interactive_test_state.last_prompt, "先确认要改哪个模块");
    }

    free(msg.content);
    reset_interactive_test_state();
    report("interactive request routes delegate child to parent context", ok);
}

static void test_delegate_task_store_persists_pending_interactive_request(void)
{
    delegate_task_store_reset_for_test();

    int ok = delegate_task_store_start_coordinator("dc_pending_req", "chat_pending_req", "", "", "parallel") == 0;
    if (ok) {
        ok = delegate_task_store_start("dt_pending_req",
                                       "dc_pending_req",
                                       "delegate_sync_pending_req",
                                       "explore",
                                       "",
                                       "pending request persistence",
                                       "prompt",
                                       "deepseek-v4-pro",
                                       "/home/wangergou/code/github/daima-agent/kernel",
                                       "subsystem",
                                       "turn_interview",
                                       NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_attach_task("dc_pending_req", "dt_pending_req") == 0;
    }
    if (ok) {
        ok = delegate_task_store_set_pending_request("dt_pending_req",
                                                     "question_text",
                                                     "question_req_store_1",
                                                     "请确认是否继续拆分 kernel/turn 模块") == 0;
    }

    delegate_task_record_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    if (ok) {
        ok = delegate_task_store_snapshot("dt_pending_req", &snapshot) == 0 &&
             strcmp(snapshot.pending_request.request_type, "question_text") == 0 &&
             strcmp(snapshot.pending_request.request_id, "question_req_store_1") == 0 &&
             strcmp(snapshot.pending_request.prompt_text, "请确认是否继续拆分 kernel/turn 模块") == 0 &&
             snapshot.child_session.question_count == 1 &&
             strcmp(snapshot.child_session.questions[0].request_type, "question_text") == 0 &&
             strcmp(snapshot.child_session.questions[0].request_id, "question_req_store_1") == 0 &&
             strcmp(snapshot.child_session.questions[0].prompt_text, "请确认是否继续拆分 kernel/turn 模块") == 0 &&
             snapshot.child_session.frame_count >= 2 &&
             strcmp(snapshot.child_session.frames[snapshot.child_session.frame_count - 1].type, "subagent_request") == 0 &&
             strcmp(snapshot.child_session.frames[snapshot.child_session.frame_count - 1].status, "blocked") == 0 &&
             strstr(snapshot.child_session.frames[snapshot.child_session.frame_count - 1].detail, "请确认是否继续拆分 kernel/turn 模块") != NULL &&
             snapshot.child_session.commit_count >= 2 &&
             strcmp(snapshot.child_session.commits[snapshot.child_session.commit_count - 1].kind, "question") == 0 &&
             strcmp(snapshot.child_session.commits[snapshot.child_session.commit_count - 1].phase, "blocked") == 0 &&
             strstr(snapshot.child_session.commits[snapshot.child_session.commit_count - 1].text, "请确认是否继续拆分 kernel/turn 模块") != NULL;
    }
    if (!ok) {
        pr_info("  pending_req initial diag: pending_type=%s pending_id=%s question_count=%d frame_count=%d commit_count=%d last_frame=%s last_frame_status=%s last_frame_detail=%s last_commit_kind=%s last_commit_phase=%s last_commit_text=%s",
                snapshot.pending_request.request_type,
                snapshot.pending_request.request_id,
                snapshot.child_session.question_count,
                snapshot.child_session.frame_count,
                snapshot.child_session.commit_count,
                snapshot.child_session.frame_count > 0
                    ? snapshot.child_session.frames[snapshot.child_session.frame_count - 1].type
                    : "",
                snapshot.child_session.frame_count > 0
                    ? snapshot.child_session.frames[snapshot.child_session.frame_count - 1].status
                    : "",
                snapshot.child_session.frame_count > 0
                    ? snapshot.child_session.frames[snapshot.child_session.frame_count - 1].detail
                    : "",
                snapshot.child_session.commit_count > 0
                    ? snapshot.child_session.commits[snapshot.child_session.commit_count - 1].kind
                    : "",
                snapshot.child_session.commit_count > 0
                    ? snapshot.child_session.commits[snapshot.child_session.commit_count - 1].phase
                    : "",
                snapshot.child_session.commit_count > 0
                    ? snapshot.child_session.commits[snapshot.child_session.commit_count - 1].text
                    : "");
    }
    if (ok) {
        ok = delegate_task_store_clear_blocked("dt_pending_req") == 0;
    }
    if (ok) {
        memset(&snapshot, 0, sizeof(snapshot));
        ok = delegate_task_store_snapshot("dt_pending_req", &snapshot) == 0 &&
             snapshot.pending_request.request_type[0] == '\0' &&
             snapshot.pending_request.request_id[0] == '\0' &&
             snapshot.pending_request.prompt_text[0] == '\0';
    }
    report("delegate task store persists pending interactive request", ok);
}

static void test_turn_context_persists_parent_pending_interactive_request(void)
{
    struct turn_snapshot snap;
    struct turn_snapshot loaded;
    memset(&snap, 0, sizeof(snap));
    memset(&loaded, 0, sizeof(loaded));

    strscpy(snap.chat_id, "parent_pending_interview_chat", sizeof(snap.chat_id));
    strscpy(snap.channel, CHAN_WEBSOCKET, sizeof(snap.channel));
    strscpy(snap.source, MSG_SOURCE_USER, sizeof(snap.source));
    turn_context_remove(snap.chat_id);
    turn_context_save(&snap);

    int ok = turn_context_set_pending_request(snap.chat_id,
                                              "question_text",
                                              "question_parent_snapshot_1",
                                              "请先确认这轮只做架构分析还是直接改代码");
    if (ok) {
        ok = turn_context_load_copy(snap.chat_id, &loaded);
    }
    if (ok) {
        ok = strcmp(loaded.pending_request_type, "question_text") == 0 &&
             strcmp(loaded.pending_request_id, "question_parent_snapshot_1") == 0 &&
             strcmp(loaded.pending_request_prompt, "请先确认这轮只做架构分析还是直接改代码") == 0;
    }
    turn_context_snapshot_cleanup(&loaded);
    if (ok) {
        ok = turn_context_clear_pending_request(snap.chat_id, "question_text", "question_parent_snapshot_1");
    }
    if (ok) {
        memset(&loaded, 0, sizeof(loaded));
        ok = turn_context_load_copy(snap.chat_id, &loaded) &&
             loaded.pending_request_type[0] == '\0' &&
             loaded.pending_request_id[0] == '\0' &&
             loaded.pending_request_prompt[0] == '\0';
    }
    turn_context_snapshot_cleanup(&loaded);
    turn_context_remove(snap.chat_id);
    report("turn context persists parent pending interactive request", ok);
}

static void test_delegate_parent_wake_sends_done_after_completion(void)
{
    reset_delegate_wake_test_env();

    int ok = delegate_task_store_start_coordinator("dc_wake_done", "chat_wake_done", "", "", "parallel") == 0;
    if (ok) {
        ok = delegate_task_store_start("dt_wake_done", "dc_wake_done", "delegate_sync_done", "explore",
                                       "", "wake completion", "prompt", "deepseek-v4-pro", "drivers/tool", "subsystem", "tool_runtime", NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_attach_task("dc_wake_done", "dt_wake_done") == 0;
    }
    if (ok) {
        ok = delegate_task_store_mark_parent_response_sent("chat_wake_done") == 0;
    }
    if (ok) {
        session_store_clear("delegate_sync_done");
        session_store_append("delegate_sync_done", "user", "scan drivers/tool");
        session_store_append("delegate_sync_done", "assistant",
                             "{\"text\":\"done summary\",\"reasoning\":\"drivers/tool scan complete\"}");
    }

    agent_loop_poll_delegate_coordinators();
    if (ok) {
        ok = delegate_task_store_complete("dt_wake_done", "done summary", "", false) == 0;
    }

    agent_loop_poll_delegate_coordinators();
    if (ok) {
        ok = s_delegate_wake_test_state.done_calls == 1 &&
             strstr(s_delegate_wake_test_state.last_done_payload, "\"coordinator_id\":\"dc_wake_done\"") &&
             strstr(s_delegate_wake_test_state.last_done_payload, "\"status\":\"done\"") &&
             strstr(s_delegate_wake_test_state.last_done_payload, "\"scope_path\":\"drivers/tool\"") &&
             strstr(s_delegate_wake_test_state.last_done_payload, "\"analysis_focus\":\"tool_runtime\"") &&
             strstr(s_delegate_wake_test_state.last_done_payload, "\"child_session\":{") &&
             strstr(s_delegate_wake_test_state.last_done_payload, "\"history\":[") &&
             strstr(s_delegate_wake_test_state.last_done_payload, "\"reasoning\":\"drivers/tool scan complete\"") &&
             strstr(s_delegate_wake_test_state.last_done_payload, "\"frames\":[") &&
             strstr(s_delegate_wake_test_state.last_done_payload, "\"commits\":[") &&
             strstr(s_delegate_wake_test_state.last_done_payload, "\"summary\":\"done summary");
    }

    struct message resume_msg;
    memset(&resume_msg, 0, sizeof(resume_msg));
    if (ok) {
        ok = message_bus_pop_inbound(&resume_msg, 1000) == 0 &&
             strcmp(resume_msg.channel, CHAN_WEBSOCKET) == 0 &&
             strcmp(resume_msg.chat_id, "chat_wake_done") == 0 &&
             strcmp(resume_msg.source, MSG_SOURCE_DELEGATE) == 0 &&
             resume_msg.content &&
             strstr(resume_msg.content, "\"coordinator_id\":\"dc_wake_done\"") &&
             strstr(resume_msg.content, "\"summary\":\"done summary\"") &&
             strstr(resume_msg.content, "\"scope_path\":\"drivers/tool\"") &&
             strstr(resume_msg.content, "\"analysis_focus\":\"tool_runtime\"");
    }
    free(resume_msg.content);
    free(resume_msg.reasoning);
    free(resume_msg.image_path);

    struct message duplicate_msg;
    memset(&duplicate_msg, 0, sizeof(duplicate_msg));
    agent_loop_poll_delegate_coordinators();
    if (ok) {
        ok = message_bus_pop_inbound(&duplicate_msg, 0) != 0;
    }
    free(duplicate_msg.content);
    free(duplicate_msg.reasoning);
    free(duplicate_msg.image_path);

    delegate_coordinator_record_t record;
    memset(&record, 0, sizeof(record));
    if (ok) {
        ok = delegate_task_store_snapshot_coordinator("dc_wake_done", &record) == 0 &&
             record.completion_notified &&
             record.parent_resume_enqueued &&
             record.wake_state == DELEGATE_WAKE_COMPLETED;
    }
    if (!ok) {
        pr_info("  wake_done diag: done_calls=%d", s_delegate_wake_test_state.done_calls);
        pr_info("  wake_done payload: %s", s_delegate_wake_test_state.last_done_payload);
        pr_info("  wake_done resume: %s", resume_msg.content ? resume_msg.content : "(null)");
    }

    report("delegate parent wake sends completion after task done", ok);
}

static void test_delegate_parent_wake_subagent_event_exposes_visible_output(void)
{
    reset_delegate_wake_test_env();

    int ok = delegate_task_store_start_coordinator("dc_wake_visible", "chat_wake_visible", "", "", "parallel") == 0;
    if (ok) {
        ok = delegate_task_store_start("dt_wake_visible",
                                       "dc_wake_visible",
                                       "delegate_sync_visible",
                                       "explore",
                                       "",
                                       "merge dependency summaries",
                                       "prompt",
                                       "deepseek-v4-pro",
                                       "kernel/tooling",
                                       "subsystem",
                                       "delegate",
                                       NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_attach_task("dc_wake_visible", "dt_wake_visible") == 0;
    }
    if (ok) {
        ok = delegate_task_store_mark_parent_response_sent("chat_wake_visible") == 0;
    }
    if (ok) {
        ok = delegate_task_store_complete("dt_wake_visible",
                                          "{\"status\":\"done\",\"summary\":\"Readable final summary\",\"evidence\":[\"kernel/tooling owns parent wake lifecycle\"]}",
                                          "",
                                          false) == 0;
    }

    agent_loop_poll_delegate_coordinators();
    if (ok) {
        ok = s_delegate_wake_test_state.subagent_calls >= 1 &&
             strcmp(s_delegate_wake_test_state.last_event_type, "subagent_done") == 0 &&
             strstr(s_delegate_wake_test_state.last_subagent_output, "\"summary\":\"Readable final summary\"") != NULL &&
             strcmp(s_delegate_wake_test_state.last_subagent_visible_output,
                    "Readable final summary\n\nEvidence:\n- kernel/tooling owns parent wake lifecycle") == 0;
    }
    if (!ok) {
        pr_info("  wake_visible event=%s output=%s visible=%s",
                s_delegate_wake_test_state.last_event_type,
                s_delegate_wake_test_state.last_subagent_output,
                s_delegate_wake_test_state.last_subagent_visible_output);
    }

    report("delegate parent wake subagent event exposes visible output", ok);
}

static void test_delegate_parent_wake_retries_failed_done_send(void)
{
    reset_delegate_wake_test_env();
    s_delegate_wake_test_state.fail_done_once = 1;

    int ok = delegate_task_store_start_coordinator("dc_wake_retry", "chat_wake_retry", "", "", "parallel") == 0;
    if (ok) {
        ok = delegate_task_store_start("dt_wake_retry", "dc_wake_retry", "delegate_sync_retry", "explore",
                                       "", "wake retry", "prompt", "deepseek-v4-pro", "drivers/tool", "subsystem", "tool_runtime", NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_attach_task("dc_wake_retry", "dt_wake_retry") == 0;
    }
    if (ok) {
        ok = delegate_task_store_mark_parent_response_sent("chat_wake_retry") == 0;
    }
    if (ok) {
        ok = delegate_task_store_complete("dt_wake_retry", "retry summary", "", false) == 0;
    }

    agent_loop_poll_delegate_coordinators();
    if (ok) {
        ok = s_delegate_wake_test_state.done_calls == 1;
    }

    usleep(1100000);
    agent_loop_poll_delegate_coordinators();
    if (ok) {
        ok = s_delegate_wake_test_state.done_calls == 2;
    }

    delegate_coordinator_record_t record;
    memset(&record, 0, sizeof(record));
    if (ok) {
        ok = delegate_task_store_snapshot_coordinator("dc_wake_retry", &record) == 0 &&
             record.completion_notified &&
             record.wake_state == DELEGATE_WAKE_COMPLETED &&
             record.wake_retry_count == 1;
    }
    if (!ok) {
        pr_info("  wake_retry diag: done_calls=%d pending=%d status_calls=%d output_calls=%d",
                s_delegate_wake_test_state.done_calls,
                delegate_parent_wake_pending_count_for_test(),
                s_delegate_wake_test_state.status_calls,
                s_delegate_wake_test_state.output_calls);
        pr_info("  wake_retry payload: %s", s_delegate_wake_test_state.last_done_payload);
        if (delegate_task_store_snapshot_coordinator("dc_wake_retry", &record) == 0) {
            pr_info("  wake_retry state: completion_notified=%d wake_state=%d retries=%d last_error=%s visible=%lu sent=%lu",
                    record.completion_notified ? 1 : 0,
                    (int)record.wake_state,
                    record.wake_retry_count,
                    record.wake_last_error,
                    record.visible_revision,
                    record.last_sent_revision);
        }
    }

    report("delegate parent wake retries failed completion send", ok);
}

static void test_delegate_parent_wake_does_not_complete_empty_output(void)
{
    reset_delegate_wake_test_env();

    int ok = delegate_task_store_start_coordinator("dc_wake_empty", "chat_wake_empty", "", "", "parallel") == 0;
    if (ok) {
        ok = delegate_task_store_start("dt_wake_empty", "dc_wake_empty", "delegate_sync_empty", "explore",
                                       "", "wake empty", "prompt", "deepseek-v4-pro", "kernel", "subsystem", "execution_kernel", NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_attach_task("dc_wake_empty", "dt_wake_empty") == 0;
    }
    if (ok) {
        ok = delegate_task_store_mark_parent_response_sent("chat_wake_empty") == 0;
    }
    if (ok) {
        ok = delegate_task_store_complete("dt_wake_empty", "sudo password was not provided", "", false) == 0;
    }

    agent_loop_poll_delegate_coordinators();

    delegate_coordinator_record_t record;
    memset(&record, 0, sizeof(record));
    if (ok) {
        ok = delegate_task_store_snapshot_coordinator("dc_wake_empty", &record) == 0 &&
             strcmp(record.status, "failed") == 0 &&
             record.effective_output_count == 0 &&
             s_delegate_wake_test_state.done_calls == 1 &&
             strstr(s_delegate_wake_test_state.last_done_payload, "\"status\":\"failed\"") != NULL &&
             strstr(s_delegate_wake_test_state.last_done_payload, "\"effective_output_count\":0") != NULL;
    }

    report("delegate parent wake does not complete empty output as success", ok);
}

static void test_delegate_parent_wake_retries_missing_parent_client(void)
{
    reset_delegate_wake_test_env();
    s_delegate_wake_test_state.fail_status_once = 1;

    int ok = delegate_task_store_start_coordinator("dc_wake_missing", "chat_wake_missing", "", "", "parallel") == 0;
    if (ok) {
        ok = delegate_task_store_start("dt_wake_missing", "dc_wake_missing", "delegate_sync_missing", "explore",
                                       "", "wake missing", "prompt", "deepseek-v4-pro", "kernel", "subsystem", "execution_kernel", NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_attach_task("dc_wake_missing", "dt_wake_missing") == 0;
    }
    if (ok) {
        ok = delegate_task_store_mark_parent_response_sent("chat_wake_missing") == 0;
    }

    agent_loop_poll_delegate_coordinators();
    if (ok) {
        ok = s_delegate_wake_test_state.status_calls == 1 &&
             s_delegate_wake_test_state.output_calls == 0 &&
             delegate_parent_wake_pending_count_for_test() == 1;
    }

    usleep(1100000);
    agent_loop_poll_delegate_coordinators();
    if (ok) {
        ok = s_delegate_wake_test_state.status_calls == 2 &&
             s_delegate_wake_test_state.output_calls == 1 &&
             s_delegate_wake_test_state.subagent_calls >= 1;
    }

    delegate_coordinator_record_t record;
    memset(&record, 0, sizeof(record));
    if (ok) {
        ok = delegate_task_store_snapshot_coordinator("dc_wake_missing", &record) == 0 &&
             record.wake_retry_count == 1 &&
             record.last_sent_revision == record.visible_revision;
    }
    if (!ok) {
        pr_info("  wake_missing diag: status_calls=%d output_calls=%d subagent_calls=%d pending=%d",
                s_delegate_wake_test_state.status_calls,
                s_delegate_wake_test_state.output_calls,
                s_delegate_wake_test_state.subagent_calls,
                delegate_parent_wake_pending_count_for_test());
        pr_info("  wake_missing status payload: %s", s_delegate_wake_test_state.last_status_payload);
        pr_info("  wake_missing output payload: %s", s_delegate_wake_test_state.last_output_payload);
    }

    report("delegate parent wake retries missing parent client", ok);
}

static void test_delegate_parent_wake_terminal_missing_parent_client_completes(void)
{
    int pending_after_second_poll = -1;
    reset_delegate_wake_test_env();
    s_delegate_wake_test_state.fail_status_once = 1;

    int ok = delegate_task_store_start_coordinator("dc_wake_missing_done", "chat_wake_missing_done", "", "", "parallel") == 0;
    if (ok) {
        ok = delegate_task_store_start("dt_wake_missing_done", "dc_wake_missing_done", "delegate_sync_missing_done", "explore",
                                       "", "wake missing done", "prompt", "deepseek-v4-pro", "kernel", "subsystem", "execution_kernel", NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_attach_task("dc_wake_missing_done", "dt_wake_missing_done") == 0;
    }
    if (ok) {
        ok = delegate_task_store_mark_parent_response_sent("chat_wake_missing_done") == 0;
    }
    if (ok) {
        ok = delegate_task_store_complete("dt_wake_missing_done", "done summary", "", false) == 0;
    }

    agent_loop_poll_delegate_coordinators();
    if (ok) {
        ok = s_delegate_wake_test_state.status_calls == 1;
    }

    delegate_coordinator_record_t record;
    memset(&record, 0, sizeof(record));
    if (ok) {
        ok = delegate_task_store_snapshot_coordinator("dc_wake_missing_done", &record) == 0 &&
             record.completion_notified &&
             record.wake_state == DELEGATE_WAKE_COMPLETED &&
             record.wake_retry_count == 0;
    }
    if (ok) {
        agent_loop_poll_delegate_coordinators();
        pending_after_second_poll = delegate_parent_wake_pending_count_for_test();
        ok = pending_after_second_poll == 0;
    }

    report("delegate parent wake terminal missing parent client completes", ok);
}

static void test_delegate_parent_wake_defers_while_parent_recently_active(void)
{
    reset_delegate_wake_test_env();
    delegate_parent_wake_set_activity_window_for_test(2000);

    int ok = delegate_task_store_start_coordinator("dc_wake_busy", "chat_wake_busy", "", "", "parallel") == 0;
    if (ok) {
        ok = delegate_task_store_start("dt_wake_busy", "dc_wake_busy", "delegate_sync_busy", "explore",
                                       "", "wake busy", "prompt", "deepseek-v4-pro", "kernel/tooling", "subsystem", "coordination", NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_attach_task("dc_wake_busy", "dt_wake_busy") == 0;
    }
    if (ok) {
        ok = delegate_task_store_mark_parent_response_sent("chat_wake_busy") == 0;
    }
    if (ok) {
        delegate_parent_wake_record_parent_activity("chat_wake_busy");
    }
    if (ok) {
        ok = delegate_task_store_complete("dt_wake_busy", "busy summary", "", false) == 0;
    }

    agent_loop_poll_delegate_coordinators();
    if (ok) {
        ok = s_delegate_wake_test_state.status_calls == 1 &&
             s_delegate_wake_test_state.output_calls == 1 &&
             s_delegate_wake_test_state.done_calls == 1 &&
             delegate_parent_wake_pending_count_for_test() == 1;
    }

    delegate_coordinator_record_t record;
    memset(&record, 0, sizeof(record));
    if (ok) {
        ok = delegate_task_store_snapshot_coordinator("dc_wake_busy", &record) == 0 &&
             record.completion_notified &&
             !record.parent_resume_enqueued &&
             record.wake_state == DELEGATE_WAKE_DISPATCHED;
    }

    usleep(2100000);
    agent_loop_poll_delegate_coordinators();
    if (ok) {
        memset(&record, 0, sizeof(record));
        ok = delegate_task_store_snapshot_coordinator("dc_wake_busy", &record) == 0 &&
             record.parent_resume_enqueued &&
             record.wake_state == DELEGATE_WAKE_COMPLETED &&
             s_delegate_wake_test_state.status_calls == 1 &&
             s_delegate_wake_test_state.output_calls == 1 &&
             s_delegate_wake_test_state.done_calls == 1 &&
             s_delegate_wake_test_state.subagent_calls >= 1;
    }
    if (!ok) {
        pr_info("  wake_busy diag: status_calls=%d output_calls=%d done_calls=%d subagent_calls=%d pending=%d",
                s_delegate_wake_test_state.status_calls,
                s_delegate_wake_test_state.output_calls,
                s_delegate_wake_test_state.done_calls,
                s_delegate_wake_test_state.subagent_calls,
                delegate_parent_wake_pending_count_for_test());
        if (delegate_task_store_snapshot_coordinator("dc_wake_busy", &record) == 0) {
            pr_info("  wake_busy state: completion_notified=%d parent_resume_enqueued=%d wake_state=%d visible=%lu sent=%lu",
                    record.completion_notified ? 1 : 0,
                    record.parent_resume_enqueued ? 1 : 0,
                    (int)record.wake_state,
                    record.visible_revision,
                    record.last_sent_revision);
        }
    }

    delegate_parent_wake_set_activity_window_for_test(2000);
    report("delegate parent wake defers while parent recently active", ok);
}

static void test_delegate_parent_wake_retains_terminal_resume_after_visible_dispatch(void)
{
    reset_delegate_wake_test_env();
    delegate_parent_wake_set_activity_window_for_test(2000);

    int ok = delegate_task_store_start_coordinator("dc_wake_resume_hold", "chat_wake_resume_hold", "", "", "parallel") == 0;
    if (ok) {
        ok = delegate_task_store_start("dt_wake_resume_hold", "dc_wake_resume_hold", "delegate_sync_resume_hold", "explore",
                                       "", "wake resume hold", "prompt", "deepseek-v4-pro", "kernel/tooling", "subsystem", "coordination", NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_attach_task("dc_wake_resume_hold", "dt_wake_resume_hold") == 0;
    }
    if (ok) {
        ok = delegate_task_store_mark_parent_response_sent("chat_wake_resume_hold") == 0;
    }
    if (ok) {
        ok = delegate_task_store_complete("dt_wake_resume_hold", "resume hold summary", "", false) == 0;
    }
    if (ok) {
        delegate_parent_wake_record_parent_activity("chat_wake_resume_hold");
    }

    agent_loop_poll_delegate_coordinators();
    if (ok) {
        ok = s_delegate_wake_test_state.status_calls == 1 &&
             s_delegate_wake_test_state.output_calls == 1 &&
             s_delegate_wake_test_state.done_calls == 1 &&
             delegate_parent_wake_pending_count_for_test() == 1;
    }

    delegate_coordinator_record_t record;
    memset(&record, 0, sizeof(record));
    if (ok) {
        ok = delegate_task_store_snapshot_coordinator("dc_wake_resume_hold", &record) == 0 &&
             record.completion_notified &&
             !record.parent_resume_enqueued &&
             record.wake_state == DELEGATE_WAKE_DISPATCHED;
    }

    struct message resume_msg;
    memset(&resume_msg, 0, sizeof(resume_msg));
    if (ok) {
        ok = message_bus_pop_inbound(&resume_msg, 0) != 0;
    }

    usleep(2100000);
    agent_loop_poll_delegate_coordinators();
    if (ok) {
        ok = message_bus_pop_inbound(&resume_msg, 1000) == 0 &&
             strcmp(resume_msg.channel, CHAN_WEBSOCKET) == 0 &&
             strcmp(resume_msg.chat_id, "chat_wake_resume_hold") == 0 &&
             strcmp(resume_msg.source, MSG_SOURCE_DELEGATE) == 0 &&
             resume_msg.content &&
             strstr(resume_msg.content, "\"coordinator_id\":\"dc_wake_resume_hold\"") &&
             strstr(resume_msg.content, "\"summary\":\"resume hold summary\"");
    }
    free(resume_msg.content);
    free(resume_msg.reasoning);
    free(resume_msg.image_path);

    if (ok) {
        memset(&record, 0, sizeof(record));
        ok = delegate_task_store_snapshot_coordinator("dc_wake_resume_hold", &record) == 0 &&
             record.parent_resume_enqueued &&
             record.wake_state == DELEGATE_WAKE_COMPLETED &&
             delegate_parent_wake_pending_count_for_test() == 0;
    }
    if (!ok) {
        pr_info("  wake_resume_hold diag: status_calls=%d output_calls=%d done_calls=%d subagent_calls=%d pending=%d",
                s_delegate_wake_test_state.status_calls,
                s_delegate_wake_test_state.output_calls,
                s_delegate_wake_test_state.done_calls,
                s_delegate_wake_test_state.subagent_calls,
                delegate_parent_wake_pending_count_for_test());
        if (delegate_task_store_snapshot_coordinator("dc_wake_resume_hold", &record) == 0) {
            pr_info("  wake_resume_hold state: completion_notified=%d parent_resume_enqueued=%d wake_state=%d visible=%lu sent=%lu",
                    record.completion_notified ? 1 : 0,
                    record.parent_resume_enqueued ? 1 : 0,
                    (int)record.wake_state,
                    record.visible_revision,
                    record.last_sent_revision);
        }
    }

    delegate_parent_wake_set_activity_window_for_test(2000);
    report("delegate parent wake retains terminal resume after visible dispatch", ok);
}

static void test_delegate_parent_wake_drops_retained_resume_after_parent_activity(void)
{
    reset_delegate_wake_test_env();
    delegate_parent_wake_set_activity_window_for_test(2000);

    delegate_coordinator_record_t consumed_record;
    unsigned long consumed_visible_revision = 0;
    struct turn_snapshot parent_snap;
    memset(&parent_snap, 0, sizeof(parent_snap));
    strscpy(parent_snap.chat_id, "chat_wake_resume_consumed", sizeof(parent_snap.chat_id));
    strscpy(parent_snap.channel, CHAN_WEBSOCKET, sizeof(parent_snap.channel));
    strscpy(parent_snap.source, "user", sizeof(parent_snap.source));
    turn_context_save(&parent_snap);

    int ok = delegate_task_store_start_coordinator("dc_wake_resume_consumed", "chat_wake_resume_consumed", "", "", "parallel") == 0;
    if (ok) {
        ok = delegate_task_store_start("dt_wake_resume_consumed", "dc_wake_resume_consumed", "delegate_sync_resume_consumed", "explore",
                                       "", "wake resume consumed", "prompt", "deepseek-v4-pro", "kernel/tooling", "subsystem", "coordination", NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_attach_task("dc_wake_resume_consumed", "dt_wake_resume_consumed") == 0;
    }
    if (ok) {
        ok = delegate_task_store_mark_parent_response_sent("chat_wake_resume_consumed") == 0;
    }
    if (ok) {
        ok = delegate_task_store_complete("dt_wake_resume_consumed", "resume consumed summary", "", false) == 0;
    }
    if (ok) {
        delegate_parent_wake_record_parent_activity("chat_wake_resume_consumed");
    }

    agent_loop_poll_delegate_coordinators();
    if (ok) {
        ok = s_delegate_wake_test_state.status_calls == 1 &&
             s_delegate_wake_test_state.output_calls == 1 &&
             s_delegate_wake_test_state.done_calls == 1 &&
             delegate_parent_wake_pending_count_for_test() == 1;
    }

    if (ok) {
        usleep(500000);
        delegate_parent_wake_record_parent_activity("chat_wake_resume_consumed");
    }

    memset(&consumed_record, 0, sizeof(consumed_record));
    if (ok) {
        ok = delegate_task_store_snapshot_coordinator("dc_wake_resume_consumed",
                                                      &consumed_record) == 0 &&
             consumed_record.visible_revision > 0;
    }
    if (ok) {
        consumed_visible_revision = consumed_record.visible_revision;
    }

    if (ok) {
        struct message consumed_msg;
        char consumed_payload[1024];
        memset(&consumed_msg, 0, sizeof(consumed_msg));
        strscpy(consumed_msg.channel, CHAN_WEBSOCKET, sizeof(consumed_msg.channel));
        strscpy(consumed_msg.chat_id, "chat_wake_resume_consumed", sizeof(consumed_msg.chat_id));
        strscpy(consumed_msg.source, MSG_SOURCE_DELEGATE, sizeof(consumed_msg.source));
        snprintf(consumed_payload,
                 sizeof(consumed_payload),
            "Delegate coordinator completed. Summarize the finished subagent outputs for the user directly.\n\n"
            "Coordinator snapshot:\n"
            "{"
              "\"coordinator_id\":\"dc_wake_resume_consumed\","
              "\"visible_revision\":%lu,"
              "\"status\":\"done\","
              "\"agents\":["
                "{"
                  "\"task_id\":\"dt_wake_resume_consumed\","
                  "\"subagent_type\":\"explore\","
                  "\"description\":\"wake resume consumed\","
                  "\"status\":\"done\","
                  "\"output\":\"resume consumed summary\""
                "}"
              "]"
            "}",
                 consumed_visible_revision);
        consumed_msg.content = strdup(consumed_payload);
        agent_turn_process_new_message(&consumed_msg);
        drain_outbound_bus_for_test();
    }

    struct message resume_msg;
    memset(&resume_msg, 0, sizeof(resume_msg));

    usleep(2100000);
    agent_loop_poll_delegate_coordinators();
    if (ok) {
        ok = message_bus_pop_inbound(&resume_msg, 0) != 0;
    }
    free(resume_msg.content);
    free(resume_msg.reasoning);
    free(resume_msg.image_path);

    delegate_coordinator_record_t record;
    memset(&record, 0, sizeof(record));
    if (ok) {
        ok = delegate_task_store_snapshot_coordinator("dc_wake_resume_consumed", &record) == 0 &&
             record.completion_notified &&
             !record.parent_resume_enqueued &&
             record.wake_state == DELEGATE_WAKE_COMPLETED &&
             delegate_parent_wake_pending_count_for_test() == 0;
    }
    if (!ok) {
        pr_info("  wake_resume_consumed diag: status_calls=%d output_calls=%d done_calls=%d subagent_calls=%d pending=%d",
                s_delegate_wake_test_state.status_calls,
                s_delegate_wake_test_state.output_calls,
                s_delegate_wake_test_state.done_calls,
                s_delegate_wake_test_state.subagent_calls,
                delegate_parent_wake_pending_count_for_test());
        if (delegate_task_store_snapshot_coordinator("dc_wake_resume_consumed", &record) == 0) {
            pr_info("  wake_resume_consumed state: completion_notified=%d parent_resume_enqueued=%d wake_state=%d visible=%lu sent=%lu",
                    record.completion_notified ? 1 : 0,
                    record.parent_resume_enqueued ? 1 : 0,
                    (int)record.wake_state,
                    record.visible_revision,
                    record.last_sent_revision);
        }
    }

    delegate_parent_wake_set_activity_window_for_test(2000);
    report("delegate parent wake drops retained resume after parent activity", ok);
}

static void test_delegate_parent_wake_recent_activity_alone_does_not_drop_retained_resume(void)
{
    reset_delegate_wake_test_env();
    delegate_parent_wake_set_activity_window_for_test(2000);

    int ok = delegate_task_store_start_coordinator("dc_wake_resume_activity_only",
                                                   "chat_wake_resume_activity_only",
                                                   "",
                                                   "",
                                                   "parallel") == 0;
    if (ok) {
        ok = delegate_task_store_start("dt_wake_resume_activity_only",
                                       "dc_wake_resume_activity_only",
                                       "delegate_sync_resume_activity_only",
                                       "explore",
                                       "",
                                       "wake resume activity only",
                                       "prompt",
                                       "deepseek-v4-pro",
                                       "kernel/tooling",
                                       "subsystem",
                                       "coordination",
                                       NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_attach_task("dc_wake_resume_activity_only",
                                             "dt_wake_resume_activity_only") == 0;
    }
    if (ok) {
        ok = delegate_task_store_mark_parent_response_sent("chat_wake_resume_activity_only") == 0;
    }
    if (ok) {
        ok = delegate_task_store_complete("dt_wake_resume_activity_only",
                                          "resume activity only summary",
                                          "",
                                          false) == 0;
    }
    if (ok) {
        delegate_parent_wake_record_parent_activity("chat_wake_resume_activity_only");
    }

    agent_loop_poll_delegate_coordinators();
    if (ok) {
        ok = delegate_parent_wake_pending_count_for_test() == 1;
    }
    if (ok) {
        usleep(500000);
        delegate_parent_wake_record_parent_activity("chat_wake_resume_activity_only");
    }

    struct message resume_msg;
    memset(&resume_msg, 0, sizeof(resume_msg));
    usleep(2100000);
    agent_loop_poll_delegate_coordinators();
    if (ok) {
        ok = message_bus_pop_inbound(&resume_msg, 1000) == 0 &&
             strcmp(resume_msg.channel, CHAN_WEBSOCKET) == 0 &&
             strcmp(resume_msg.chat_id, "chat_wake_resume_activity_only") == 0 &&
             strcmp(resume_msg.source, MSG_SOURCE_DELEGATE) == 0 &&
             resume_msg.content &&
             strstr(resume_msg.content, "\"coordinator_id\":\"dc_wake_resume_activity_only\"") &&
             strstr(resume_msg.content, "\"summary\":\"resume activity only summary\"");
    }
    free(resume_msg.content);
    free(resume_msg.reasoning);
    free(resume_msg.image_path);

    delegate_coordinator_record_t record;
    memset(&record, 0, sizeof(record));
    if (ok) {
        ok = delegate_task_store_snapshot_coordinator("dc_wake_resume_activity_only", &record) == 0 &&
             record.parent_resume_enqueued &&
             record.wake_state == DELEGATE_WAKE_COMPLETED &&
             delegate_parent_wake_pending_count_for_test() == 0;
    }
    if (!ok) {
        pr_info("  wake_resume_activity_only diag: status_calls=%d output_calls=%d done_calls=%d pending=%d",
                s_delegate_wake_test_state.status_calls,
                s_delegate_wake_test_state.output_calls,
                s_delegate_wake_test_state.done_calls,
                delegate_parent_wake_pending_count_for_test());
        if (delegate_task_store_snapshot_coordinator("dc_wake_resume_activity_only", &record) == 0) {
            pr_info("  wake_resume_activity_only state: completion_notified=%d parent_resume_enqueued=%d wake_state=%d visible=%lu sent=%lu",
                    record.completion_notified ? 1 : 0,
                    record.parent_resume_enqueued ? 1 : 0,
                    (int)record.wake_state,
                    record.visible_revision,
                    record.last_sent_revision);
        }
    }

    delegate_parent_wake_set_activity_window_for_test(2000);
    report("delegate parent wake recent activity alone does not drop retained resume", ok);
}

static void test_delegate_parent_wake_drops_retained_resume_after_parent_assistant_output(void)
{
    reset_delegate_wake_test_env();
    delegate_parent_wake_set_activity_window_for_test(2000);

    struct turn_snapshot parent_snap;
    memset(&parent_snap, 0, sizeof(parent_snap));
    strscpy(parent_snap.chat_id, "chat_wake_resume_assistant_output", sizeof(parent_snap.chat_id));
    strscpy(parent_snap.channel, CHAN_WEBSOCKET, sizeof(parent_snap.channel));
    strscpy(parent_snap.source, "user", sizeof(parent_snap.source));
    parent_snap.messages = cJSON_CreateArray();
    turn_context_save(&parent_snap);
    turn_context_snapshot_cleanup(&parent_snap);

    int ok = delegate_task_store_start_coordinator("dc_wake_resume_assistant_output",
                                                   "chat_wake_resume_assistant_output",
                                                   "",
                                                   "",
                                                   "parallel") == 0;
    if (ok) {
        ok = delegate_task_store_start("dt_wake_resume_assistant_output",
                                       "dc_wake_resume_assistant_output",
                                       "delegate_sync_resume_assistant_output",
                                       "explore",
                                       "",
                                       "wake resume assistant output",
                                       "prompt",
                                       "deepseek-v4-pro",
                                       "kernel/tooling",
                                       "subsystem",
                                       "coordination",
                                       NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_attach_task("dc_wake_resume_assistant_output",
                                             "dt_wake_resume_assistant_output") == 0;
    }
    if (ok) {
        ok = delegate_task_store_mark_parent_response_sent("chat_wake_resume_assistant_output") == 0;
    }
    if (ok) {
        ok = delegate_task_store_complete("dt_wake_resume_assistant_output",
                                          "resume assistant output summary",
                                          "",
                                          false) == 0;
    }
    if (ok) {
        delegate_parent_wake_record_parent_activity("chat_wake_resume_assistant_output");
    }

    agent_loop_poll_delegate_coordinators();
    if (ok) {
        ok = delegate_parent_wake_pending_count_for_test() == 1;
    }

    if (ok) {
        struct turn_snapshot assistant_snap;
        memset(&assistant_snap, 0, sizeof(assistant_snap));
        strscpy(assistant_snap.chat_id, "chat_wake_resume_assistant_output", sizeof(assistant_snap.chat_id));
        strscpy(assistant_snap.channel, CHAN_WEBSOCKET, sizeof(assistant_snap.channel));
        strscpy(assistant_snap.source, "user", sizeof(assistant_snap.source));
        assistant_snap.messages = cJSON_CreateArray();
        cJSON *assistant_msg = cJSON_CreateObject();
        cJSON_AddStringToObject(assistant_msg, "role", "assistant");
        cJSON_AddStringToObject(assistant_msg, "content", "parent already produced visible assistant output");
        cJSON_AddItemToArray(assistant_snap.messages, assistant_msg);
        turn_context_save(&assistant_snap);
        turn_context_snapshot_cleanup(&assistant_snap);
    }

    struct message resume_msg;
    memset(&resume_msg, 0, sizeof(resume_msg));
    usleep(2100000);
    agent_loop_poll_delegate_coordinators();
    if (ok) {
        ok = message_bus_pop_inbound(&resume_msg, 0) != 0;
    }
    free(resume_msg.content);
    free(resume_msg.reasoning);
    free(resume_msg.image_path);

    delegate_coordinator_record_t record;
    memset(&record, 0, sizeof(record));
    if (ok) {
        ok = delegate_task_store_snapshot_coordinator("dc_wake_resume_assistant_output", &record) == 0 &&
             record.completion_notified &&
             !record.parent_resume_enqueued &&
             record.wake_state == DELEGATE_WAKE_COMPLETED &&
             delegate_parent_wake_pending_count_for_test() == 0;
    }
    if (!ok) {
        pr_info("  wake_resume_assistant_output diag: status_calls=%d output_calls=%d done_calls=%d pending=%d",
                s_delegate_wake_test_state.status_calls,
                s_delegate_wake_test_state.output_calls,
                s_delegate_wake_test_state.done_calls,
                delegate_parent_wake_pending_count_for_test());
        if (delegate_task_store_snapshot_coordinator("dc_wake_resume_assistant_output", &record) == 0) {
            pr_info("  wake_resume_assistant_output state: completion_notified=%d parent_resume_enqueued=%d wake_state=%d visible=%lu sent=%lu",
                    record.completion_notified ? 1 : 0,
                    record.parent_resume_enqueued ? 1 : 0,
                    (int)record.wake_state,
                    record.visible_revision,
                    record.last_sent_revision);
        }
    }

    delegate_parent_wake_set_activity_window_for_test(2000);
    report("delegate parent wake drops retained resume after parent assistant output", ok);
}

static void test_delegate_parent_wake_retains_failed_resume_after_parent_activity(void)
{
    reset_delegate_wake_test_env();
    delegate_parent_wake_set_activity_window_for_test(2000);

    int ok = delegate_task_store_start_coordinator("dc_wake_failed_resume", "chat_wake_failed_resume", "", "", "parallel") == 0;
    if (ok) {
        ok = delegate_task_store_start("dt_wake_failed_resume", "dc_wake_failed_resume", "delegate_sync_failed_resume", "explore",
                                       "", "wake failed resume", "prompt", "deepseek-v4-pro", "kernel/tooling", "subsystem", "coordination", NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_attach_task("dc_wake_failed_resume", "dt_wake_failed_resume") == 0;
    }
    if (ok) {
        ok = delegate_task_store_mark_parent_response_sent("chat_wake_failed_resume") == 0;
    }
    if (ok) {
        ok = delegate_task_store_complete("dt_wake_failed_resume", "sudo password was not provided", "", false) == 0;
    }
    if (ok) {
        delegate_parent_wake_record_parent_activity("chat_wake_failed_resume");
    }

    agent_loop_poll_delegate_coordinators();
    if (ok) {
        ok = s_delegate_wake_test_state.status_calls == 1 &&
             s_delegate_wake_test_state.output_calls == 1 &&
             s_delegate_wake_test_state.done_calls == 1 &&
             delegate_parent_wake_pending_count_for_test() == 1;
    }

    if (ok) {
        usleep(500000);
        delegate_parent_wake_record_parent_activity("chat_wake_failed_resume");
    }

    struct message resume_msg;
    memset(&resume_msg, 0, sizeof(resume_msg));

    usleep(2100000);
    agent_loop_poll_delegate_coordinators();
    if (ok) {
        ok = message_bus_pop_inbound(&resume_msg, 1000) == 0 &&
             strcmp(resume_msg.channel, CHAN_WEBSOCKET) == 0 &&
             strcmp(resume_msg.chat_id, "chat_wake_failed_resume") == 0 &&
             strcmp(resume_msg.source, MSG_SOURCE_DELEGATE) == 0 &&
             resume_msg.content &&
             strstr(resume_msg.content, "\"coordinator_id\":\"dc_wake_failed_resume\"") &&
             strstr(resume_msg.content, "\"status\":\"failed\"");
    }
    free(resume_msg.content);
    free(resume_msg.reasoning);
    free(resume_msg.image_path);

    delegate_coordinator_record_t record;
    memset(&record, 0, sizeof(record));
    if (ok) {
        ok = delegate_task_store_snapshot_coordinator("dc_wake_failed_resume", &record) == 0 &&
             record.completion_notified &&
             record.parent_resume_enqueued &&
             record.wake_state == DELEGATE_WAKE_COMPLETED &&
             delegate_parent_wake_pending_count_for_test() == 0;
    }
    if (!ok) {
        pr_info("  wake_failed_resume diag: status_calls=%d output_calls=%d done_calls=%d subagent_calls=%d pending=%d",
                s_delegate_wake_test_state.status_calls,
                s_delegate_wake_test_state.output_calls,
                s_delegate_wake_test_state.done_calls,
                s_delegate_wake_test_state.subagent_calls,
                delegate_parent_wake_pending_count_for_test());
        if (delegate_task_store_snapshot_coordinator("dc_wake_failed_resume", &record) == 0) {
            pr_info("  wake_failed_resume state: completion_notified=%d parent_resume_enqueued=%d wake_state=%d visible=%lu sent=%lu status=%s",
                    record.completion_notified ? 1 : 0,
                    record.parent_resume_enqueued ? 1 : 0,
                    (int)record.wake_state,
                    record.visible_revision,
                    record.last_sent_revision,
                    record.status);
        }
    }

    delegate_parent_wake_set_activity_window_for_test(2000);
    report("delegate parent wake retains failed resume after parent activity", ok);
}

static void test_delegate_parent_wake_defers_resume_while_parent_has_pending_request(void)
{
    reset_delegate_wake_test_env();
    delegate_parent_wake_set_activity_window_for_test(2000);
    bool pending_set_ok = false;
    bool pending_load_ok = false;
    bool complete_ok = false;
    bool initial_hold_ok = false;
    bool retained_hold_ok = false;
    bool clear_pending_ok = false;
    bool resume_delivery_ok = false;
    bool final_state_ok = false;

    struct turn_snapshot snap;
    memset(&snap, 0, sizeof(snap));
    strscpy(snap.chat_id, "chat_wake_parent_pending", sizeof(snap.chat_id));
    strscpy(snap.channel, CHAN_WEBSOCKET, sizeof(snap.channel));
    strscpy(snap.source, MSG_SOURCE_USER, sizeof(snap.source));
    turn_context_remove(snap.chat_id);
    turn_context_save(&snap);

    int ok = delegate_task_store_start_coordinator("dc_wake_parent_pending", "chat_wake_parent_pending", "", "", "parallel") == 0;
    if (ok) {
        ok = delegate_task_store_start("dt_wake_parent_pending", "dc_wake_parent_pending", "delegate_sync_parent_pending", "explore",
                                       "", "wake parent pending", "prompt", "deepseek-v4-pro", "kernel/tooling", "subsystem", "coordination", NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_attach_task("dc_wake_parent_pending", "dt_wake_parent_pending") == 0;
    }
    if (ok) {
        ok = delegate_task_store_mark_parent_response_sent("chat_wake_parent_pending") == 0;
    }
    if (ok) {
        pending_set_ok = turn_context_set_pending_request("chat_wake_parent_pending",
                                                          "question_text",
                                                          "question_parent_pending_1",
                                                          "请先确认父会话是否继续");
        ok = pending_set_ok;
    }
    if (ok) {
        struct turn_snapshot loaded;
        memset(&loaded, 0, sizeof(loaded));
        pending_load_ok = turn_context_load_copy("chat_wake_parent_pending", &loaded) &&
                          strcmp(loaded.pending_request_type, "question_text") == 0 &&
                          strcmp(loaded.pending_request_id, "question_parent_pending_1") == 0;
        ok = pending_load_ok;
        turn_context_snapshot_cleanup(&loaded);
    }
    if (ok) {
        complete_ok = delegate_task_store_complete("dt_wake_parent_pending", "parent pending summary", "", false) == 0;
        ok = complete_ok;
    }

    agent_loop_poll_delegate_coordinators();
    if (ok) {
        initial_hold_ok = s_delegate_wake_test_state.status_calls == 1 &&
                          s_delegate_wake_test_state.output_calls == 1 &&
                          s_delegate_wake_test_state.done_calls == 1 &&
                          delegate_parent_wake_pending_count_for_test() == 1;
        ok = initial_hold_ok;
    }

    struct message resume_msg;
    memset(&resume_msg, 0, sizeof(resume_msg));
    delegate_coordinator_record_t record;
    memset(&record, 0, sizeof(record));
    if (ok) {
        initial_hold_ok = message_bus_pop_inbound(&resume_msg, 0) != 0 &&
                          delegate_task_store_snapshot_coordinator("dc_wake_parent_pending", &record) == 0 &&
                          record.completion_notified &&
                          !record.parent_resume_enqueued &&
                          record.wake_state == DELEGATE_WAKE_DISPATCHED &&
                          delegate_parent_wake_pending_count_for_test() == 1;
        ok = initial_hold_ok;
    }
    free(resume_msg.content);
    free(resume_msg.reasoning);
    free(resume_msg.image_path);

    usleep(1100000);
    agent_loop_poll_delegate_coordinators();
    memset(&resume_msg, 0, sizeof(resume_msg));
    if (ok) {
        retained_hold_ok = s_delegate_wake_test_state.status_calls == 1 &&
                           s_delegate_wake_test_state.output_calls == 1 &&
                           s_delegate_wake_test_state.done_calls == 1 &&
                           message_bus_pop_inbound(&resume_msg, 0) != 0 &&
                           delegate_task_store_snapshot_coordinator("dc_wake_parent_pending", &record) == 0 &&
                           record.completion_notified &&
                           !record.parent_resume_enqueued &&
                           record.wake_state == DELEGATE_WAKE_DISPATCHED &&
                           delegate_parent_wake_pending_count_for_test() == 1;
        ok = retained_hold_ok;
    }
    free(resume_msg.content);
    free(resume_msg.reasoning);
    free(resume_msg.image_path);

    if (ok) {
        clear_pending_ok = turn_context_clear_pending_request("chat_wake_parent_pending",
                                                              "question_text",
                                                              "question_parent_pending_1");
        ok = clear_pending_ok;
    }

    memset(&resume_msg, 0, sizeof(resume_msg));
    usleep(1100000);
    agent_loop_poll_delegate_coordinators();
    agent_loop_poll_delegate_coordinators();
    if (ok) {
        resume_delivery_ok = message_bus_pop_inbound(&resume_msg, 1000) == 0 &&
                             strcmp(resume_msg.channel, CHAN_WEBSOCKET) == 0 &&
                             strcmp(resume_msg.chat_id, "chat_wake_parent_pending") == 0 &&
                             strcmp(resume_msg.source, MSG_SOURCE_DELEGATE) == 0 &&
                             resume_msg.content &&
                             strstr(resume_msg.content, "\"coordinator_id\":\"dc_wake_parent_pending\"") &&
                             strstr(resume_msg.content, "\"summary\":\"parent pending summary\"");
        ok = resume_delivery_ok;
    }
    free(resume_msg.content);
    free(resume_msg.reasoning);
    free(resume_msg.image_path);

    if (ok) {
        memset(&record, 0, sizeof(record));
        final_state_ok = delegate_task_store_snapshot_coordinator("dc_wake_parent_pending", &record) == 0 &&
                         record.parent_resume_enqueued &&
                         record.wake_state == DELEGATE_WAKE_COMPLETED &&
                         delegate_parent_wake_pending_count_for_test() == 0;
        ok = final_state_ok;
    }
    if (!ok) {
        pr_info("  wake_parent_pending phases: initial_hold_ok=%d retained_hold_ok=%d clear_pending_ok=%d resume_delivery_ok=%d final_state_ok=%d",
                initial_hold_ok ? 1 : 0,
                retained_hold_ok ? 1 : 0,
                clear_pending_ok ? 1 : 0,
                resume_delivery_ok ? 1 : 0,
                final_state_ok ? 1 : 0);
        pr_info("  wake_parent_pending diag: status_calls=%d output_calls=%d done_calls=%d subagent_calls=%d pending=%d",
                s_delegate_wake_test_state.status_calls,
                s_delegate_wake_test_state.output_calls,
                s_delegate_wake_test_state.done_calls,
                s_delegate_wake_test_state.subagent_calls,
                delegate_parent_wake_pending_count_for_test());
        pr_info("  wake_parent_pending done payload: %s", s_delegate_wake_test_state.last_done_payload);
        if (delegate_task_store_snapshot_coordinator("dc_wake_parent_pending", &record) == 0) {
            pr_info("  wake_parent_pending state: completion_notified=%d parent_resume_enqueued=%d wake_state=%d visible=%lu sent=%lu",
                    record.completion_notified ? 1 : 0,
                    record.parent_resume_enqueued ? 1 : 0,
                    (int)record.wake_state,
                    record.visible_revision,
                    record.last_sent_revision);
        }
    }

    turn_context_remove("chat_wake_parent_pending");
    delegate_parent_wake_set_activity_window_for_test(2000);
    report("delegate parent wake defers resume while parent has pending request", ok);
}

static void test_delegate_parent_wake_does_not_repeat_same_visible_revision(void)
{
    reset_delegate_wake_test_env();
    delegate_parent_wake_set_activity_window_for_test(2000);

    int ok = delegate_task_store_start_coordinator("dc_wake_dedupe", "chat_wake_dedupe", "", "", "parallel") == 0;
    if (ok) {
        ok = delegate_task_store_start("dt_wake_dedupe", "dc_wake_dedupe", "delegate_sync_dedupe", "explore",
                                       "", "wake dedupe", "prompt", "deepseek-v4-pro", "kernel/tooling", "subsystem", "coordination", NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_attach_task("dc_wake_dedupe", "dt_wake_dedupe") == 0;
    }
    if (ok) {
        ok = delegate_task_store_mark_parent_response_sent("chat_wake_dedupe") == 0;
    }
    if (ok) {
        delegate_parent_wake_record_parent_activity("chat_wake_dedupe");
    }

    agent_loop_poll_delegate_coordinators();
    if (ok) {
        ok = s_delegate_wake_test_state.status_calls == 1 &&
             s_delegate_wake_test_state.output_calls == 1 &&
             s_delegate_wake_test_state.subagent_calls >= 1 &&
             delegate_parent_wake_pending_count_for_test() == 0;
    }

    usleep(2100000);
    agent_loop_poll_delegate_coordinators();
    if (ok) {
        ok = s_delegate_wake_test_state.status_calls == 1 &&
             s_delegate_wake_test_state.output_calls == 1 &&
             s_delegate_wake_test_state.subagent_calls >= 1;
    }

    delegate_parent_wake_record_parent_activity("chat_wake_dedupe");
    agent_loop_poll_delegate_coordinators();
    usleep(2100000);
    agent_loop_poll_delegate_coordinators();
    if (ok) {
        ok = s_delegate_wake_test_state.status_calls == 1 &&
             s_delegate_wake_test_state.output_calls == 1 &&
             s_delegate_wake_test_state.subagent_calls >= 1;
    }
    if (!ok) {
        pr_info("  wake_dedupe diag: status_calls=%d output_calls=%d subagent_calls=%d pending=%d",
                s_delegate_wake_test_state.status_calls,
                s_delegate_wake_test_state.output_calls,
                s_delegate_wake_test_state.subagent_calls,
                delegate_parent_wake_pending_count_for_test());
    }

    report("delegate parent wake dedupes same visible revision", ok);
}

static void test_delegate_parent_wake_ignores_unchanged_coordinators(void)
{
    reset_delegate_wake_test_env();

    int ok = delegate_task_store_start_coordinator("dc_wake_unchanged", "chat_wake_unchanged", "", "", "parallel") == 0;
    if (ok) {
        ok = delegate_task_store_start("dt_wake_unchanged", "dc_wake_unchanged", "delegate_sync_unchanged", "explore",
                                       "", "wake unchanged", "prompt", "deepseek-v4-pro", "kernel/tooling", "subsystem", "coordination", NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_attach_task("dc_wake_unchanged", "dt_wake_unchanged") == 0;
    }
    if (ok) {
        ok = delegate_task_store_mark_parent_response_sent("chat_wake_unchanged") == 0;
    }

    agent_loop_poll_delegate_coordinators();
    if (ok) {
        ok = s_delegate_wake_test_state.status_calls == 1 &&
             s_delegate_wake_test_state.output_calls == 1 &&
             delegate_parent_wake_pending_count_for_test() == 0;
    }

    agent_loop_poll_delegate_coordinators();
    agent_loop_poll_delegate_coordinators();
    if (ok) {
        ok = s_delegate_wake_test_state.status_calls == 1 &&
             s_delegate_wake_test_state.output_calls == 1 &&
             delegate_parent_wake_pending_count_for_test() == 0;
    }
    if (!ok) {
        pr_info("  wake_unchanged diag: status_calls=%d output_calls=%d done_calls=%d subagent_calls=%d pending=%d",
                s_delegate_wake_test_state.status_calls,
                s_delegate_wake_test_state.output_calls,
                s_delegate_wake_test_state.done_calls,
                s_delegate_wake_test_state.subagent_calls,
                delegate_parent_wake_pending_count_for_test());
    }

    report("delegate parent wake ignores unchanged coordinators", ok);
}

static void test_delegate_parent_subagent_state_json_uses_shared_projection(void)
{
    delegate_task_store_reset_for_test();
    turn_context_remove("chat_projection");

    struct turn_snapshot snap;
    memset(&snap, 0, sizeof(snap));
    strscpy(snap.chat_id, "chat_projection", sizeof(snap.chat_id));
    strscpy(snap.channel, CHAN_WEBSOCKET, sizeof(snap.channel));
    strscpy(snap.source, MSG_SOURCE_USER, sizeof(snap.source));
    turn_context_save(&snap);

    int ok = delegate_task_store_start_coordinator("dc_projection", "chat_projection", "team_run_projection", "Team Projection", "parallel") == 0;
    if (ok) {
        ok = delegate_task_store_start("dt_projection",
                                       "dc_projection",
                                       "delegate_sync_projection",
                                       "explore",
                                       "projection_task",
                                       "projection detail",
                                       "prompt",
                                       "deepseek-v4-pro",
                                       "kernel/tooling/delegate",
                                       "subsystem",
                                       "projection",
                                       NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_attach_task("dc_projection", "dt_projection") == 0;
    }
    if (ok) {
        session_store_clear("delegate_sync_projection");
        session_store_append("delegate_sync_projection", "user", "分析多 subagent 投影");
        session_store_append("delegate_sync_projection",
                             "assistant",
                             "{\"text\":\"HTTP 和 WS 应共享同一个投影层。\",\"reasoning\":\"projection reasoning\"}");
        ok = delegate_task_store_set_pending_request("dt_projection",
                                                     "question_text",
                                                     "question_projection_child_1",
                                                     "子任务还需要补充哪个字段") == 0;
    }
    if (ok) {
        ok = turn_context_set_pending_request("chat_projection",
                                              "question_text",
                                              "question_projection_parent_1",
                                              "父会话是否继续等待全部子任务");
    }

    char *json = ok ? delegate_parent_subagent_state_json_build("chat_projection") : NULL;
    if (ok) {
        ok = json &&
             strstr(json, "\"chat_id\":\"chat_projection\"") &&
             strstr(json, "\"coordinator_id\":\"dc_projection\"") &&
             strstr(json, "\"team_run_id\":\"team_run_projection\"") &&
             strstr(json, "\"team_name\":\"Team Projection\"") &&
             strstr(json, "\"dispatch_mode\":\"parallel\"") &&
             strstr(json, "\"task_id\":\"dt_projection\"") &&
             strstr(json, "\"task_key\":\"projection_task\"") &&
             strstr(json, "\"session_id\":\"delegate_sync_projection\"") &&
             strstr(json, "\"analysis_focus\":\"projection\"") &&
             strstr(json, "\"pending_request\":{\"request_type\":\"question_text\",\"request_id\":\"question_projection_parent_1\",\"prompt\":\"父会话是否继续等待全部子任务\"}") &&
             strstr(json, "\"child_session\":{") &&
             strstr(json, "\"history\":[") &&
             strstr(json, "\"content\":\"HTTP 和 WS 应共享同一个投影层。\"") &&
             strstr(json, "\"reasoning\":\"projection reasoning\"") &&
             strstr(json, "\"pending_request\":{\"request_type\":\"question_text\",\"request_id\":\"question_projection_child_1\",\"prompt\":\"子任务还需要补充哪个字段\"}") &&
             strstr(json, "\"interactive\":{\"blockers\":[") &&
             strstr(json, "\"label\":\"需要补充信息\"") &&
             strstr(json, "\"label\":\"projection detail\"") &&
             strstr(json, "\"coordinator_status\":\"running\"");
    }
    if (!ok) {
        pr_info("  projection_json diag: %s", json ? json : "(null)");
    }

    free(json);
    turn_context_remove("chat_projection");
    report("delegate parent subagent state json uses shared projection", ok);
}

static void test_delegate_session_state_json_unifies_parent_history_and_subagents(void)
{
    delegate_task_store_reset_for_test();
    turn_context_remove("chat_session_state");
    session_store_clear("chat_session_state");
    session_store_clear("delegate_sync_session_state");

    struct turn_snapshot snap;
    memset(&snap, 0, sizeof(snap));
    strscpy(snap.chat_id, "chat_session_state", sizeof(snap.chat_id));
    strscpy(snap.channel, CHAN_WEBSOCKET, sizeof(snap.channel));
    strscpy(snap.source, MSG_SOURCE_USER, sizeof(snap.source));
    turn_context_save(&snap);

    int ok = session_store_append("chat_session_state", "user", "请分析 session-first 架构") == 0;
    if (ok) {
        ok = session_store_append("chat_session_state",
                                  "assistant",
                                  "{\"text\":\"先统一 session restore 协议。\",\"reasoning\":\"session-state reasoning\"}") == 0;
    }
    if (ok) {
        ok = delegate_task_store_start_coordinator("dc_session_state",
                                                   "chat_session_state",
                                                   "team_run_session_state",
                                                   "Team Session State",
                                                   "parallel") == 0;
    }
    if (ok) {
        ok = delegate_task_store_start("dt_session_state",
                                       "dc_session_state",
                                       "delegate_sync_session_state",
                                       "explore",
                                       "session_state_task",
                                       "分析 session state",
                                       "prompt",
                                       "deepseek-v4-pro",
                                       "kernel/tooling/delegate",
                                       "subsystem",
                                       "projection",
                                       NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_attach_task("dc_session_state", "dt_session_state") == 0;
    }
    if (ok) {
        ok = session_store_append("delegate_sync_session_state", "user", "查看 child session 投影") == 0;
    }
    if (ok) {
        ok = session_store_append("delegate_sync_session_state",
                                  "assistant",
                                  "{\"text\":\"child session 应成为 restore 真相源。\",\"reasoning\":\"child reasoning\"}") == 0;
    }

    char request[256];
    snprintf(request,
             sizeof(request),
             "GET /api/session_state?chat_id=chat_session_state HTTP/1.1\r\nHost: localhost\r\n\r\n");
    int fds[2] = {-1, -1};
    char response[32768];
    ssize_t n = -1;
    if (ok) {
        ok = socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0;
    }
    if (ok) {
        ok = ws_http_handle_request(fds[0], request, "<html></html>") == 0;
    }
    if (ok) {
        n = self_test_recv_all_http_response(fds[1], response, sizeof(response));
        ok = n > 0;
    }
    if (n > 0) {
        response[n] = '\0';
    } else {
        response[0] = '\0';
    }

    const char *body = strstr(response, "\r\n\r\n");
    body = body ? body + 4 : response;
    if (ok) {
        cJSON *json = cJSON_Parse(body);
        cJSON *chat_id = json ? cJSON_GetObjectItemCaseSensitive(json, "chat_id") : NULL;
        cJSON *history = json ? cJSON_GetObjectItemCaseSensitive(json, "history") : NULL;
        cJSON *first = history && cJSON_IsArray(history) ? cJSON_GetArrayItem(history, 0) : NULL;
        cJSON *second = history && cJSON_IsArray(history) ? cJSON_GetArrayItem(history, 1) : NULL;
        cJSON *history_window = json ? cJSON_GetObjectItemCaseSensitive(json, "history_window") : NULL;
        cJSON *history_cursor = json ? cJSON_GetObjectItemCaseSensitive(json, "history_cursor") : NULL;
        cJSON *subagent = json ? cJSON_GetObjectItemCaseSensitive(json, "subagent") : NULL;
        cJSON *coordinators = subagent ? cJSON_GetObjectItemCaseSensitive(subagent, "coordinators") : NULL;
        cJSON *coordinator = coordinators && cJSON_IsArray(coordinators) ? cJSON_GetArrayItem(coordinators, 0) : NULL;
        cJSON *agents = coordinator ? cJSON_GetObjectItemCaseSensitive(coordinator, "agents") : NULL;
        cJSON *agent = agents && cJSON_IsArray(agents) ? cJSON_GetArrayItem(agents, 0) : NULL;
        cJSON *ui = json ? cJSON_GetObjectItemCaseSensitive(json, "ui") : NULL;
        cJSON *assistant_content = second ? cJSON_GetObjectItemCaseSensitive(second, "content") : NULL;
        cJSON *assistant_reasoning = second ? cJSON_GetObjectItemCaseSensitive(second, "reasoning") : NULL;
        cJSON *window_count = history_window ? cJSON_GetObjectItemCaseSensitive(history_window, "count") : NULL;
        cJSON *window_total = history_window ? cJSON_GetObjectItemCaseSensitive(history_window, "total") : NULL;
        cJSON *window_truncated = history_window ? cJSON_GetObjectItemCaseSensitive(history_window, "truncated") : NULL;
        cJSON *window_first_seq = history_window ? cJSON_GetObjectItemCaseSensitive(history_window, "first_seq") : NULL;
        cJSON *window_last_seq = history_window ? cJSON_GetObjectItemCaseSensitive(history_window, "last_seq") : NULL;
        cJSON *window_high_water_seq = history_window ? cJSON_GetObjectItemCaseSensitive(history_window, "high_water_seq") : NULL;
        cJSON *window_next_seq = history_window ? cJSON_GetObjectItemCaseSensitive(history_window, "next_seq") : NULL;
        cJSON *window_has_more = history_window ? cJSON_GetObjectItemCaseSensitive(history_window, "has_more") : NULL;
        cJSON *cursor_after_seq = history_cursor ? cJSON_GetObjectItemCaseSensitive(history_cursor, "after_seq") : NULL;
        cJSON *cursor_visible_seq = history_cursor ? cJSON_GetObjectItemCaseSensitive(history_cursor, "visible_seq") : NULL;
        cJSON *cursor_first_visible_seq = history_cursor ? cJSON_GetObjectItemCaseSensitive(history_cursor, "first_visible_seq") : NULL;
        cJSON *cursor_next_seq = history_cursor ? cJSON_GetObjectItemCaseSensitive(history_cursor, "next_seq") : NULL;
        cJSON *cursor_high_water_seq = history_cursor ? cJSON_GetObjectItemCaseSensitive(history_cursor, "high_water_seq") : NULL;
        cJSON *cursor_has_more = history_cursor ? cJSON_GetObjectItemCaseSensitive(history_cursor, "has_more") : NULL;
        cJSON *cursor_replay_reset = history_cursor ? cJSON_GetObjectItemCaseSensitive(history_cursor, "replay_reset") : NULL;
        cJSON *coordinator_id = coordinator ? cJSON_GetObjectItemCaseSensitive(coordinator, "coordinator_id") : NULL;
        cJSON *session_id = agent ? cJSON_GetObjectItemCaseSensitive(agent, "session_id") : NULL;
        cJSON *agent_summary = agent ? cJSON_GetObjectItemCaseSensitive(agent, "summary") : NULL;
        cJSON *agent_child_session = agent ? cJSON_GetObjectItemCaseSensitive(agent, "child_session") : NULL;
        cJSON *agent_child_history = agent_child_session ? cJSON_GetObjectItemCaseSensitive(agent_child_session, "history") : NULL;

        ok = json &&
             cJSON_IsString(chat_id) &&
             strcmp(chat_id->valuestring, "chat_session_state") == 0 &&
             history && cJSON_IsArray(history) &&
             cJSON_GetArraySize(history) == 2 &&
             first && second &&
             cJSON_IsString(assistant_content) &&
             strcmp(assistant_content->valuestring, "先统一 session restore 协议。") == 0 &&
             cJSON_IsString(assistant_reasoning) &&
             strcmp(assistant_reasoning->valuestring, "session-state reasoning") == 0 &&
             history_window && cJSON_IsObject(history_window) &&
             history_cursor && cJSON_IsObject(history_cursor) &&
             cJSON_IsNumber(window_count) && (long long)window_count->valuedouble == 2 &&
             cJSON_IsNumber(window_total) && (long long)window_total->valuedouble == 2 &&
             cJSON_IsBool(window_truncated) && cJSON_IsFalse(window_truncated) &&
             cJSON_IsNumber(window_first_seq) && (long long)window_first_seq->valuedouble == 1 &&
             cJSON_IsNumber(window_last_seq) && (long long)window_last_seq->valuedouble == 2 &&
             cJSON_IsNumber(window_high_water_seq) && (long long)window_high_water_seq->valuedouble == 2 &&
             cJSON_IsNumber(window_next_seq) && (long long)window_next_seq->valuedouble == 3 &&
             cJSON_IsBool(window_has_more) && cJSON_IsFalse(window_has_more) &&
             cJSON_IsNumber(cursor_after_seq) && (long long)cursor_after_seq->valuedouble == 0 &&
             cJSON_IsNumber(cursor_visible_seq) && (long long)cursor_visible_seq->valuedouble == 2 &&
             cJSON_IsNumber(cursor_first_visible_seq) && (long long)cursor_first_visible_seq->valuedouble == 1 &&
             cJSON_IsNumber(cursor_next_seq) && (long long)cursor_next_seq->valuedouble == 3 &&
             cJSON_IsNumber(cursor_high_water_seq) && (long long)cursor_high_water_seq->valuedouble == 2 &&
             cJSON_IsBool(cursor_has_more) && cJSON_IsFalse(cursor_has_more) &&
             cJSON_IsBool(cursor_replay_reset) && cJSON_IsFalse(cursor_replay_reset) &&
             subagent && cJSON_IsObject(subagent) &&
             coordinator && cJSON_IsObject(coordinator) &&
             cJSON_IsString(coordinator_id) &&
             strcmp(coordinator_id->valuestring, "dc_session_state") == 0 &&
             agent && cJSON_IsObject(agent) &&
             cJSON_IsString(session_id) &&
             strcmp(session_id->valuestring, "delegate_sync_session_state") == 0 &&
             cJSON_IsString(agent_summary) &&
             strstr(agent_summary->valuestring, "child session 应成为 restore 真相源。") != NULL &&
             agent_child_session && cJSON_IsObject(agent_child_session) &&
             agent_child_history && cJSON_IsArray(agent_child_history) &&
             cJSON_GetArraySize(agent_child_history) == 2 &&
             ui && cJSON_IsObject(ui);
        if (!ok) {
            char *coord_json = coordinator ? cJSON_PrintUnformatted(coordinator) : NULL;
            char *agent_json = agent ? cJSON_PrintUnformatted(agent) : NULL;
            pr_info("  session_state_json parsed diag: history_size=%d window_count=%lld window_total=%lld window_truncated=%d window_has_more=%d cursor_has_more=%d cursor_replay_reset=%d coord=%s agent=%s",
                    history && cJSON_IsArray(history) ? cJSON_GetArraySize(history) : -1,
                    cJSON_IsNumber(window_count) ? (long long)window_count->valuedouble : -1LL,
                    cJSON_IsNumber(window_total) ? (long long)window_total->valuedouble : -1LL,
                    cJSON_IsBool(window_truncated) ? cJSON_IsTrue(window_truncated) : -1,
                    cJSON_IsBool(window_has_more) ? cJSON_IsTrue(window_has_more) : -1,
                    cJSON_IsBool(cursor_has_more) ? cJSON_IsTrue(cursor_has_more) : -1,
                    cJSON_IsBool(cursor_replay_reset) ? cJSON_IsTrue(cursor_replay_reset) : -1,
                    coord_json ? coord_json : "<null>",
                    agent_json ? agent_json : "<null>");
            kfree(coord_json);
            kfree(agent_json);
        }
        cJSON_Delete(json);
    }
    if (!ok) {
        pr_info("  session_state_json diag: %s", body && body[0] ? body : response);
    }

    if (fds[0] >= 0) close(fds[0]);
    if (fds[1] >= 0) close(fds[1]);
    turn_context_remove("chat_session_state");
    report("delegate session state json unifies parent history and subagents", ok);
}

static void test_delegate_subagent_session_delta_json_uses_incremental_projection(void)
{
    delegate_task_store_reset_for_test();
    session_store_clear("delegate_sync_delta_payload");

    delegate_task_record_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));

    int ok = delegate_task_store_start("dt_delta_payload",
                                       "dc_delta_payload",
                                       "delegate_sync_delta_payload",
                                       "explore",
                                       "delta-payload-task",
                                       "delta payload detail",
                                       "prompt",
                                       "deepseek-v4-pro",
                                       "kernel/tooling",
                                       "subsystem",
                                       "coordination",
                                       NULL) == 0;
    if (ok) {
        for (int i = 0; i < 6; i++) {
            char content[96];
            snprintf(content, sizeof(content), "delta payload history %02d", i);
            if (session_store_append_ex("delegate_sync_delta_payload",
                                        (i % 2) == 0 ? "user" : "assistant",
                                        content,
                                        (i % 2) == 0 ? "user" : "delegate_child") != 0) {
                ok = 0;
                break;
            }
        }
    }
    if (ok) {
        for (int i = 0; i < 4; i++) {
            char detail[96];
            snprintf(detail, sizeof(detail), "delta payload step %02d", i);
            if (delegate_task_store_append_session_step("dt_delta_payload", "tool", detail, detail) != 0) {
                ok = 0;
                break;
            }
        }
    }
    if (ok) {
        ok = delegate_task_store_complete("dt_delta_payload", "delta payload summary", "", false) == 0;
    }
    if (ok) {
        ok = delegate_task_store_snapshot("dt_delta_payload", &snapshot) == 0;
    }

    char *json = ok ? delegate_subagent_session_delta_json_build("dt_delta_payload", 4, 2, 2) : NULL;
    if (ok) {
        ok = json &&
             strstr(json, "\"task_id\":\"dt_delta_payload\"") &&
             strstr(json, "\"history_after_seq\":4") &&
             strstr(json, "\"frame_after_seq\":2") &&
             strstr(json, "\"commit_after_seq\":2") &&
             strstr(json, "\"history_first_seq\":5") &&
             strstr(json, "\"frame_first_seq\":3") &&
             strstr(json, "\"commit_first_seq\":3") &&
             strstr(json, "\"replay_reset\":false");
    }
    if (!ok) {
        pr_info("  delta_payload_json diag: %s", json ? json : "(null)");
    }

    free(json);
    report("delegate subagent session delta json uses incremental projection", ok);
}

static void test_delegate_subagent_session_deltas_json_batches_incremental_projection(void)
{
    delegate_task_store_reset_for_test();
    session_store_clear("delegate_sync_batch_delta_a");
    session_store_clear("delegate_sync_batch_delta_b");

    int ok = delegate_task_store_start_coordinator("dc_batch_delta",
                                                   "chat_batch_delta",
                                                   "team_run_batch_delta",
                                                   "Team Batch Delta",
                                                   "parallel") == 0;
    if (ok) {
        ok = delegate_task_store_start("dt_batch_delta_a",
                                       "dc_batch_delta",
                                       "delegate_sync_batch_delta_a",
                                       "explore",
                                       "batch-delta-a",
                                       "batch delta task A",
                                       "prompt A",
                                       "deepseek-v4-pro",
                                       "kernel/turn",
                                       "subsystem",
                                       "projection",
                                       NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_start("dt_batch_delta_b",
                                       "dc_batch_delta",
                                       "delegate_sync_batch_delta_b",
                                       "explore",
                                       "batch-delta-b",
                                       "batch delta task B",
                                       "prompt B",
                                       "deepseek-v4-pro",
                                       "kernel/tooling",
                                       "subsystem",
                                       "projection",
                                       NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_attach_task("dc_batch_delta", "dt_batch_delta_a") == 0 &&
             delegate_task_store_attach_task("dc_batch_delta", "dt_batch_delta_b") == 0;
    }
    if (ok) {
        for (int i = 0; i < 4; i++) {
            char content[96];
            snprintf(content, sizeof(content), "batch delta A history %02d", i);
            if (session_store_append_ex("delegate_sync_batch_delta_a",
                                        (i % 2) == 0 ? "user" : "assistant",
                                        content,
                                        (i % 2) == 0 ? "user" : "delegate_child") != 0) {
                ok = 0;
                break;
            }
        }
    }
    if (ok) {
        for (int i = 0; i < 3; i++) {
            char content[96];
            snprintf(content, sizeof(content), "batch delta B history %02d", i);
            if (session_store_append_ex("delegate_sync_batch_delta_b",
                                        (i % 2) == 0 ? "user" : "assistant",
                                        content,
                                        (i % 2) == 0 ? "user" : "delegate_child") != 0) {
                ok = 0;
                break;
            }
        }
    }
    if (ok) {
        for (int i = 0; i < 3; i++) {
            char detail[96];
            snprintf(detail, sizeof(detail), "batch delta A step %02d", i);
            if (delegate_task_store_append_session_step("dt_batch_delta_a", "tool", detail, detail) != 0) {
                ok = 0;
                break;
            }
        }
    }
    if (ok) {
        for (int i = 0; i < 2; i++) {
            char detail[96];
            snprintf(detail, sizeof(detail), "batch delta B step %02d", i);
            if (delegate_task_store_append_session_step("dt_batch_delta_b", "tool", detail, detail) != 0) {
                ok = 0;
                break;
            }
        }
    }
    if (ok) {
        ok = delegate_task_store_complete("dt_batch_delta_a", "batch delta summary A", "", false) == 0 &&
             delegate_task_store_complete("dt_batch_delta_b", "batch delta summary B", "", false) == 0;
    }

    const char *request_json =
        "{"
        "\"chat_id\":\"chat_batch_delta\","
        "\"tasks\":["
          "{"
            "\"task_id\":\"dt_batch_delta_a\","
            "\"history_after_seq\":2,"
            "\"frame_after_seq\":1,"
            "\"commit_after_seq\":1"
          "},"
          "{"
            "\"task_id\":\"dt_batch_delta_b\","
            "\"history_after_seq\":1,"
            "\"frame_after_seq\":0,"
            "\"commit_after_seq\":0"
          "}"
        "]"
        "}";

    char *json = ok ? delegate_subagent_session_deltas_json_build("chat_batch_delta", request_json) : NULL;
    if (ok) {
        ok = json &&
             strstr(json, "\"chat_id\":\"chat_batch_delta\"") &&
             strstr(json, "\"item_count\":2") &&
             strstr(json, "\"task_id\":\"dt_batch_delta_a\"") &&
             strstr(json, "\"task_id\":\"dt_batch_delta_b\"") &&
             strstr(json, "\"history_after_seq\":2") &&
             strstr(json, "\"history_after_seq\":1") &&
             strstr(json, "\"frame_after_seq\":1") &&
             strstr(json, "\"commit_after_seq\":1") &&
             strstr(json, "\"history_first_seq\":3") &&
             strstr(json, "\"history_first_seq\":2") &&
             strstr(json, "\"replay_reset\":false");
    }
    if (!ok) {
        pr_info("  batch_delta_json diag: %s", json ? json : "(null)");
    }

    free(json);
    report("delegate subagent session deltas json batches incremental projection", ok);
}

static void test_delegate_parent_subagent_state_delta_json_batches_visible_revision_and_sessions(void)
{
    delegate_task_store_reset_for_test();
    session_store_clear("delegate_sync_chat_delta_a");
    session_store_clear("delegate_sync_chat_delta_b");

    int ok = delegate_task_store_start_coordinator("dc_chat_delta",
                                                   "chat_delta_projection",
                                                   "team_run_chat_delta",
                                                   "Team Chat Delta",
                                                   "parallel") == 0;
    if (ok) {
        ok = delegate_task_store_start("dt_chat_delta_a",
                                       "dc_chat_delta",
                                       "delegate_sync_chat_delta_a",
                                       "explore",
                                       "chat-delta-a",
                                       "chat delta task A",
                                       "prompt A",
                                       "deepseek-v4-pro",
                                       "kernel/turn",
                                       "subsystem",
                                       "projection",
                                       NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_start("dt_chat_delta_b",
                                       "dc_chat_delta",
                                       "delegate_sync_chat_delta_b",
                                       "explore",
                                       "chat-delta-b",
                                       "chat delta task B",
                                       "prompt B",
                                       "deepseek-v4-pro",
                                       "kernel/tooling",
                                       "subsystem",
                                       "projection",
                                       NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_attach_task("dc_chat_delta", "dt_chat_delta_a") == 0 &&
             delegate_task_store_attach_task("dc_chat_delta", "dt_chat_delta_b") == 0;
    }
    if (ok) {
        ok = session_store_append_ex("delegate_sync_chat_delta_a",
                                     "assistant",
                                     "chat delta A history",
                                     "delegate_child") == 0 &&
             session_store_append_ex("delegate_sync_chat_delta_b",
                                     "assistant",
                                     "chat delta B history",
                                     "delegate_child") == 0;
    }
    if (ok) {
        ok = delegate_task_store_append_session_step("dt_chat_delta_a",
                                                     "tool",
                                                     "chat delta A step",
                                                     "chat delta A step") == 0 &&
             delegate_task_store_append_session_step("dt_chat_delta_b",
                                                     "tool",
                                                     "chat delta B step",
                                                     "chat delta B step") == 0;
    }
    if (ok) {
        ok = delegate_task_store_complete("dt_chat_delta_a", "chat delta summary A", "", false) == 0 &&
             delegate_task_store_complete("dt_chat_delta_b", "chat delta summary B", "", false) == 0;
    }

    delegate_coordinator_record_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    if (ok) {
        ok = delegate_task_store_snapshot_coordinator("dc_chat_delta", &snapshot) == 0 &&
             snapshot.visible_revision > 0;
    }

    char request_json[512];
    request_json[0] = '\0';
    if (ok) {
        snprintf(request_json,
                 sizeof(request_json),
                 "{"
                 "\"chat_id\":\"chat_delta_projection\","
                 "\"after_visible_revision\":%lu,"
                 "\"tasks\":["
                   "{"
                     "\"task_id\":\"dt_chat_delta_a\","
                     "\"history_after_seq\":0,"
                     "\"frame_after_seq\":0,"
                     "\"commit_after_seq\":0"
                   "},"
                   "{"
                     "\"task_id\":\"dt_chat_delta_b\","
                     "\"history_after_seq\":0,"
                     "\"frame_after_seq\":0,"
                     "\"commit_after_seq\":0"
                   "}"
                 "]"
                 "}",
                 snapshot.visible_revision - 1);
    }

    char *json = ok
        ? delegate_parent_subagent_state_delta_json_build("chat_delta_projection",
                                                          snapshot.visible_revision - 1,
                                                          request_json)
        : NULL;
    if (ok) {
        ok = json &&
             strstr(json, "\"chat_id\":\"chat_delta_projection\"") &&
             strstr(json, "\"after_visible_revision\":") &&
             strstr(json, "\"max_visible_revision\":") &&
             strstr(json, "\"changed_count\":1") &&
             strstr(json, "\"item_count\":2") &&
             strstr(json, "\"coordinator_id\":\"dc_chat_delta\"") &&
             strstr(json, "\"task_id\":\"dt_chat_delta_a\"") &&
             strstr(json, "\"task_id\":\"dt_chat_delta_b\"");
    }
    if (!ok) {
        pr_info("  chat_delta_json diag: %s", json ? json : "(null)");
    }

    free(json);
    report("delegate parent subagent state delta json batches visible revision and sessions", ok);
}

static void test_delegate_parent_subagent_state_delta_json_includes_changed_coordinator_sessions_without_explicit_tasks(void)
{
    delegate_task_store_reset_for_test();
    session_store_clear("delegate_sync_chat_delta_implicit_a");
    session_store_clear("delegate_sync_chat_delta_implicit_b");

    int ok = delegate_task_store_start_coordinator("dc_chat_delta_implicit",
                                                   "chat_delta_projection_implicit",
                                                   "team_run_chat_delta_implicit",
                                                   "Team Chat Delta Implicit",
                                                   "parallel") == 0;
    if (ok) {
        ok = delegate_task_store_start("dt_chat_delta_implicit_a",
                                       "dc_chat_delta_implicit",
                                       "delegate_sync_chat_delta_implicit_a",
                                       "explore",
                                       "chat-delta-implicit-a",
                                       "chat delta implicit task A",
                                       "prompt A",
                                       "deepseek-v4-pro",
                                       "kernel/turn",
                                       "subsystem",
                                       "projection",
                                       NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_start("dt_chat_delta_implicit_b",
                                       "dc_chat_delta_implicit",
                                       "delegate_sync_chat_delta_implicit_b",
                                       "explore",
                                       "chat-delta-implicit-b",
                                       "chat delta implicit task B",
                                       "prompt B",
                                       "deepseek-v4-pro",
                                       "kernel/tooling",
                                       "subsystem",
                                       "projection",
                                       NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_attach_task("dc_chat_delta_implicit", "dt_chat_delta_implicit_a") == 0 &&
             delegate_task_store_attach_task("dc_chat_delta_implicit", "dt_chat_delta_implicit_b") == 0;
    }
    if (ok) {
        ok = session_store_append_ex("delegate_sync_chat_delta_implicit_a",
                                     "assistant",
                                     "chat delta implicit A history",
                                     "delegate_child") == 0 &&
             session_store_append_ex("delegate_sync_chat_delta_implicit_b",
                                     "assistant",
                                     "chat delta implicit B history",
                                     "delegate_child") == 0;
    }
    if (ok) {
        ok = delegate_task_store_append_session_step("dt_chat_delta_implicit_a",
                                                     "tool",
                                                     "chat delta implicit A step",
                                                     "chat delta implicit A step") == 0 &&
             delegate_task_store_append_session_step("dt_chat_delta_implicit_b",
                                                     "tool",
                                                     "chat delta implicit B step",
                                                     "chat delta implicit B step") == 0;
    }
    if (ok) {
        ok = delegate_task_store_complete("dt_chat_delta_implicit_a", "chat delta implicit summary A", "", false) == 0 &&
             delegate_task_store_complete("dt_chat_delta_implicit_b", "chat delta implicit summary B", "", false) == 0;
    }

    delegate_coordinator_record_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    if (ok) {
        ok = delegate_task_store_snapshot_coordinator("dc_chat_delta_implicit", &snapshot) == 0 &&
             snapshot.visible_revision > 0;
    }

    char request_json[256];
    request_json[0] = '\0';
    if (ok) {
        snprintf(request_json,
                 sizeof(request_json),
                 "{"
                 "\"chat_id\":\"chat_delta_projection_implicit\","
                 "\"after_visible_revision\":%lu"
                 "}",
                 snapshot.visible_revision - 1);
    }

    char *json = ok
        ? delegate_parent_subagent_state_delta_json_build("chat_delta_projection_implicit",
                                                          snapshot.visible_revision - 1,
                                                          request_json)
        : NULL;
    if (ok) {
        ok = json &&
             strstr(json, "\"chat_id\":\"chat_delta_projection_implicit\"") &&
             strstr(json, "\"changed_count\":1") &&
             strstr(json, "\"item_count\":2") &&
             strstr(json, "\"task_id\":\"dt_chat_delta_implicit_a\"") &&
             strstr(json, "\"task_id\":\"dt_chat_delta_implicit_b\"") &&
             strstr(json, "\"history_after_seq\":0") &&
             strstr(json, "\"frame_after_seq\":0") &&
             strstr(json, "\"commit_after_seq\":0");
    }
    if (!ok) {
        pr_info("  chat_delta_implicit_json diag: %s", json ? json : "(null)");
    }

    free(json);
    report("delegate parent subagent state delta json includes changed coordinator sessions without explicit tasks", ok);
}

static void test_delegate_parent_registry_exposes_wake_lifecycle(void)
{
    delegate_task_store_reset_for_test();
    reset_delegate_wake_test_state();

    int ok = delegate_task_store_start_coordinator("dc_wake_list", "chat_wake_list", "", "", "parallel") == 0;
    if (ok) {
        ok = delegate_task_store_start("dt_wake_list", "dc_wake_list", "delegate_sync_list", "explore",
                                       "", "wake list", "prompt", "deepseek-v4-pro", "drivers/tool", "subsystem", "tool_runtime", NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_attach_task("dc_wake_list", "dt_wake_list") == 0;
    }
    if (ok) {
        ok = delegate_task_store_mark_parent_response_sent("chat_wake_list") == 0;
    }
    agent_loop_poll_delegate_coordinators();

    struct message msg;
    memset(&msg, 0, sizeof(msg));
    strscpy(msg.channel, "system", sizeof(msg.channel));
    strscpy(msg.chat_id, "chat_wake_list", sizeof(msg.chat_id));
    strscpy(msg.source, "internal", sizeof(msg.source));
    msg.content = strdup("list");

    llm_tool_call_t call;
    memset(&call, 0, sizeof(call));
    strscpy(call.id, "tool_delegate_wake_list", sizeof(call.id));
    strscpy(call.name, "delegate_task", sizeof(call.name));
    call.input = strdup("{\"action\":\"list\"}");
    call.input_len = strlen(call.input);

    char output[8192];
    memset(output, 0, sizeof(output));
    tool_runtime_result_t rt;
    memset(&rt, 0, sizeof(rt));
    if (ok) {
        ok = tool_runtime_execute_call(&call, &msg, output, sizeof(output), &rt) == 0 &&
             strstr(output, "\"wake_state\":\"dispatched\"") &&
             strstr(output, "\"wake_retry_count\":0") &&
             strstr(output, "\"parent_response_sent\":true");
    }
    if (!ok) {
        pr_info("  wake_list diag: output=%s", output);
    }

    free(call.input);
    free(msg.content);
    report("delegate parent registry exposes wake lifecycle", ok);
}

static void test_delegate_stored_directive_shortcut_starts_background_delegate(void)
{
    struct message msg;
    memset(&msg, 0, sizeof(msg));
    strscpy(msg.channel, CHAN_WEBSOCKET, sizeof(msg.channel));
    strscpy(msg.chat_id, "chat_delegate_directive_shortcut", sizeof(msg.chat_id));
    strscpy(msg.source, MSG_SOURCE_USER, sizeof(msg.source));
    msg.intent = INTENT_IMPLEMENT;
    msg.content = strdup("先不要改代码，直接按结构化批量委托去拆范围。");

    const char *directive =
        "{"
        "\"tasks\":["
          "{"
            "\"description\":\"分析 kernel/turn\","
            "\"subagent_type\":\"explore\","
            "\"target_path\":\"/home/wangergou/code/github/daima-agent/kernel/turn\","
            "\"prompt\":\"分析 kernel/turn 的目录结构和关键模块。\""
          "},"
          "{"
            "\"description\":\"验证 sudo 权限链路\","
            "\"subagent_type\":\"explore\","
            "\"target_path\":\"/home/wangergou/code/github/daima-agent\","
            "\"prompt\":\"验证 sudo 权限链路，并说明取消时会怎样阻塞。\","
            "\"preflight_tool\":{"
              "\"tool_name\":\"terminal\","
              "\"input\":{\"command\":\"sudo ls /root\",\"workdir\":\"/home/wangergou/code/github/daima-agent\"},"
              "\"continue_on_error\":false"
            "}"
          "}"
        "]"
        "}";
    delegate_turn_directive_clear(msg.chat_id);
    int stored = delegate_turn_directive_store(msg.chat_id, directive);

    llm_tool_call_t call;
    memset(&call, 0, sizeof(call));
    strscpy(call.id, "tool_delegate_directive_shortcut", sizeof(call.id));
    strscpy(call.name, "delegate_task", sizeof(call.name));
    call.input = strdup("{\"subagent_type\":\"explore\",\"description\":\"broad discovery\",\"prompt\":\"analyze repo\"}");
    call.input_len = strlen(call.input);

    char output[8192];
    memset(output, 0, sizeof(output));
    tool_runtime_result_t rt;
    memset(&rt, 0, sizeof(rt));

    int ok = stored &&
             tool_runtime_execute_call(&call, &msg, output, sizeof(output), &rt) == 0 &&
             strstr(output, "\"coordinator_id\":\"dc_") &&
             strstr(output, "\"agent_count\":2") &&
             strstr(output, "\"dispatch_mode\":\"parallel\"") &&
             strstr(output, "\"agents\":[");
    if (!ok) {
        pr_info("  delegate_directive_shortcut diag: output=%s", output);
    }

    free(call.input);
    free(msg.content);
    delegate_turn_directive_clear(msg.chat_id);
    report("delegate stored directive shortcut starts background delegate", ok);
}

static void test_delegate_completion_turn_hides_delegate_tool(void)
{
    const char *tools_json = tool_bus_tools_json_for_channel_without_delegate(CHAN_WEBSOCKET);
    int ok = tools_json &&
             strstr(tools_json, "\"name\":\"files\"") &&
             strstr(tools_json, "\"name\":\"terminal\"") &&
             !strstr(tools_json, "\"name\":\"delegate_task\"");
    report("delegate completion turn hides delegate tool", ok);
}

static void test_tool_guard_detects_non_advertised_tool(void)
{
    const char *tools_json = tool_bus_tools_json_for_channel_without_delegate(CHAN_WEBSOCKET);
    int ok = tools_json &&
             agent_tool_name_is_advertised(tools_json, "files") &&
             !agent_tool_name_is_advertised(tools_json, "delegate_task");
    report("tool guard detects non-advertised tool", ok);
}

static void test_delegate_completion_turn_uses_no_tools(void)
{
    struct message msg;
    memset(&msg, 0, sizeof(msg));
    strscpy(msg.channel, CHAN_WEBSOCKET, sizeof(msg.channel));
    strscpy(msg.chat_id, "chat_delegate_no_tools", sizeof(msg.chat_id));
    strscpy(msg.source, MSG_SOURCE_DELEGATE, sizeof(msg.source));

    const char *tools_json =
        strcmp(agent_msg_source_or_default(&msg), MSG_SOURCE_DELEGATE) == 0
            ? NULL
            : tool_bus_tools_json_for_channel(msg.channel);

    int ok = (tools_json == NULL);
    report("delegate completion turn uses no tools", ok);
}

static void test_delegate_completion_turn_merges_locally(void)
{
    drain_inbound_bus_for_test();

    struct message msg;
    memset(&msg, 0, sizeof(msg));
    strscpy(msg.channel, CHAN_WEBSOCKET, sizeof(msg.channel));
    strscpy(msg.chat_id, "chat_delegate_local_merge", sizeof(msg.chat_id));
    strscpy(msg.source, MSG_SOURCE_DELEGATE, sizeof(msg.source));
    msg.content = strdup(
        "Delegate coordinator completed. Summarize the finished subagent outputs for the user directly.\n\n"
        "Coordinator snapshot:\n"
        "{"
          "\"coordinator_id\":\"dc_local_merge\","
          "\"status\":\"done\","
          "\"agents\":["
            "{"
              "\"task_id\":\"dt_a\","
              "\"subagent_type\":\"explore\","
              "\"description\":\"分析 kernel\","
              "\"status\":\"done\","
              "\"output\":\"kernel 目录主要负责主循环和 turn 执行链路。\""
            "},"
            "{"
              "\"task_id\":\"dt_b\","
              "\"subagent_type\":\"explore\","
              "\"description\":\"分析 drivers/tool\","
              "\"status\":\"done\","
              "\"output\":\"drivers/tool 主要负责工具协议、运行时封装和 delegate_task。\""
            "}"
          "]"
        "}");

    agent_turn_process_new_message(&msg);

    struct message out;
    memset(&out, 0, sizeof(out));
    int ok = message_bus_pop_outbound(&out, 1000) == 0 &&
             out.content &&
             strstr(out.content, "并行子任务汇总") &&
             strstr(out.content, "状态：共 2 个子任务，已完成 2 个，未完成 0 个") &&
             strstr(out.content, "关键发现：") &&
             strstr(out.content, "分析 kernel") &&
             strstr(out.content, "主循环和 turn 执行链路") &&
             strstr(out.content, "分析 drivers/tool") &&
             strstr(out.content, "工具协议、运行时封装和 delegate_task") &&
             strstr(out.content, "原始子任务摘要：") &&
             strstr(out.content, "{\"status\":\"done\"") == NULL &&
             out.reasoning == NULL;
    free(out.content);
    free(out.reasoning);
    free(out.image_path);
    free(msg.content);

    report("delegate completion turn merges locally", ok);
}

static void test_delegate_completion_turn_prefers_child_session_rendered_summary(void)
{
    drain_inbound_bus_for_test();

    struct message msg;
    memset(&msg, 0, sizeof(msg));
    strscpy(msg.channel, CHAN_WEBSOCKET, sizeof(msg.channel));
    strscpy(msg.chat_id, "chat_delegate_child_session_first", sizeof(msg.chat_id));
    strscpy(msg.source, MSG_SOURCE_DELEGATE, sizeof(msg.source));
    msg.content = strdup(
        "Delegate coordinator completed. Summarize the finished subagent outputs for the user directly.\n\n"
        "Coordinator snapshot:\n"
        "{"
          "\"coordinator_id\":\"dc_child_session_first\","
          "\"status\":\"done\","
          "\"agents\":["
            "{"
              "\"task_id\":\"dt_a\","
              "\"subagent_type\":\"explore\","
              "\"description\":\"分析 kernel\","
              "\"status\":\"done\","
              "\"summary\":\"旧摘要：只有一句过时文本。\","
              "\"output\":\"旧输出：只有一句过时文本。\","
              "\"child_session\":{"
                "\"latest_frame\":{"
                  "\"type\":\"subagent_done\","
                  "\"status\":\"done\","
                  "\"detail\":\"child done detail\","
                  "\"output_preview\":\"{\\\"status\\\":\\\"done\\\",\\\"summary\\\":\\\"kernel/turn 负责单回合执行主链，kernel/tooling 负责后台协调与 parent wake。\\\",\\\"evidence\\\":[\\\"kernel/turn/turn_entry.c\\\",\\\"kernel/tooling/delegate/delegate_parent_wake.c\\\"],\\\"risks\\\":[],\\\"next_files\\\":[\\\"kernel/tooling/delegate/delegate_state_json.c\\\"]}\""
                "},"
                "\"history\":["
                  "{"
                    "\"role\":\"assistant\","
                    "\"content\":\"{\\\"status\\\":\\\"done\\\",\\\"summary\\\":\\\"kernel/turn 负责单回合执行主链，kernel/tooling 负责后台协调与 parent wake。\\\",\\\"evidence\\\":[\\\"kernel/turn/turn_entry.c\\\",\\\"kernel/tooling/delegate/delegate_parent_wake.c\\\"],\\\"risks\\\":[],\\\"next_files\\\":[\\\"kernel/tooling/delegate/delegate_state_json.c\\\"]}\""
                  "}"
                "]"
              "}"
            "}"
          "]"
        "}");

    agent_turn_process_new_message(&msg);

    struct message out;
    memset(&out, 0, sizeof(out));
    int ok = message_bus_pop_outbound(&out, 1000) == 0 &&
             out.content &&
             strstr(out.content, "kernel/turn 负责单回合执行主链") &&
             strstr(out.content, "Evidence:") &&
             strstr(out.content, "kernel/tooling/delegate/delegate_parent_wake.c") &&
             strstr(out.content, "Next files:") &&
             strstr(out.content, "旧摘要：只有一句过时文本。") == NULL &&
             strstr(out.content, "旧输出：只有一句过时文本。") == NULL;
    free(out.content);
    free(out.reasoning);
    free(out.image_path);
    free(msg.content);

    report("delegate completion turn prefers child session rendered summary", ok);
}

static void test_delegate_background_coordinator_summary_prefers_child_session_rendered_text(void)
{
    delegate_task_store_reset_for_test();

    delegate_coordinator_record_t record;
    delegate_task_record_t snapshot;
    char summary[2048];
    memset(&record, 0, sizeof(record));
    memset(&snapshot, 0, sizeof(snapshot));
    memset(summary, 0, sizeof(summary));

    int ok = delegate_task_store_start("dt_summary_child_first",
                                       "dc_summary_child_first",
                                       "delegate_sync_summary_child_first",
                                       "explore",
                                       "summary-child-first",
                                       "分析 kernel/turn",
                                       "prompt",
                                       "deepseek-v4-pro",
                                       "kernel/turn",
                                       "subsystem",
                                       "turn_execution",
                                       NULL) == 0;
    if (ok) {
        ok = delegate_task_store_complete("dt_summary_child_first",
                                          "旧 output：过时摘要。",
                                          "",
                                          false) == 0;
    }
    if (ok) {
        ok = delegate_task_store_append_session_message(
                 "dt_summary_child_first",
                 "assistant",
                 "{\"status\":\"done\",\"summary\":\"kernel/turn 负责单回合执行主链与最终回复生成。\",\"evidence\":[\"kernel/turn/turn_entry.c\"],\"risks\":[],\"next_files\":[\"kernel/turn/turn_pipeline.c\"]}") == 0;
    }
    if (ok) {
        ok = delegate_task_store_snapshot("dt_summary_child_first", &snapshot) == 0;
    }
    if (ok) {
        record.agent_count = 1;
        strscpy(record.coordinator_id, "dc_summary_child_first", sizeof(record.coordinator_id));
        strscpy(record.status, "done", sizeof(record.status));
        record.completed_count = 1;
        record.effective_output_count = 1;
        strscpy(record.agents[0].task_id, snapshot.task_id, sizeof(record.agents[0].task_id));
        strscpy(record.agents[0].description, snapshot.description, sizeof(record.agents[0].description));
        strscpy(record.agents[0].subagent_type, snapshot.subagent_type, sizeof(record.agents[0].subagent_type));
        strscpy(record.agents[0].status, "done", sizeof(record.agents[0].status));
    }
    if (ok) {
        tool_delegate_render_background_coordinator_summary(&record, summary, sizeof(summary));
        ok = strstr(summary, "kernel/turn 负责单回合执行主链与最终回复生成") != NULL &&
             strstr(summary, "旧 output：过时摘要。") == NULL &&
             strstr(summary, "任务摘要：") != NULL;
    }

    report("delegate background coordinator summary prefers child session rendered text", ok);
}

static void test_delegate_state_json_agent_summary_prefers_child_session_rendered_text(void)
{
    delegate_coordinator_record_t snapshot;
    char *payload = NULL;
    int ok;

    delegate_task_store_reset_for_test();
    session_store_clear("delegate_sync_state_child_first");
    session_store_clear("chat_state_child_first");
    memset(&snapshot, 0, sizeof(snapshot));

    ok = delegate_task_store_start_coordinator("dc_state_child_first",
                                               "chat_state_child_first",
                                               "tr_state_child_first",
                                               "state-child-first",
                                               "parallel") == 0;
    if (ok) {
        ok = delegate_task_store_start("dt_state_child_first",
                                       "dc_state_child_first",
                                       "delegate_sync_state_child_first",
                                       "explore",
                                       "state-child-first",
                                       "分析 kernel/tooling",
                                       "prompt",
                                       "deepseek-v4-pro",
                                       "kernel/tooling",
                                       "subsystem",
                                       "coordination",
                                       NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_complete("dt_state_child_first",
                                          "旧 output：快照层不该优先显示这句。",
                                          "",
                                          false) == 0;
    }
    if (ok) {
        ok = delegate_task_store_append_session_message(
                 "dt_state_child_first",
                 "assistant",
                 "{\"status\":\"done\",\"summary\":\"kernel/tooling 负责 delegate store、projection 与 parent wake 协调。\",\"evidence\":[\"kernel/tooling/delegate/delegate_task_store.c\",\"kernel/tooling/delegate/delegate_parent_wake.c\"],\"risks\":[],\"next_files\":[\"kernel/tooling/delegate/delegate_state_json.c\"]}") == 0;
    }
    if (ok) {
        ok = delegate_task_store_attach_task("dc_state_child_first", "dt_state_child_first") == 0;
    }
    if (ok) {
        ok = delegate_task_store_snapshot_coordinator("dc_state_child_first", &snapshot) == 0;
    }
    if (ok) {
        payload = delegate_coordinator_snapshot_json_build(&snapshot, false);
        if (payload) {
            cJSON *root = cJSON_Parse(payload);
            cJSON *agents = root ? cJSON_GetObjectItem(root, "agents") : NULL;
            cJSON *agent0 = agents && cJSON_IsArray(agents) ? cJSON_GetArrayItem(agents, 0) : NULL;
            const char *summary = agent0 ? cJSON_GetStringValue(cJSON_GetObjectItem(agent0, "summary")) : NULL;
            const char *output = agent0 ? cJSON_GetStringValue(cJSON_GetObjectItem(agent0, "output")) : NULL;
            const char *raw_output = agent0 ? cJSON_GetStringValue(cJSON_GetObjectItem(agent0, "raw_output")) : NULL;
            const char *description = agent0 ? cJSON_GetStringValue(cJSON_GetObjectItem(agent0, "description")) : NULL;
            ok = root &&
                 summary &&
                 description &&
                 strcmp(description, "分析 kernel/tooling") == 0 &&
                 strstr(summary, "kernel/tooling 负责 delegate store、projection 与 parent wake 协调。") != NULL &&
                 output &&
                 strstr(output, "kernel/tooling 负责 delegate store、projection 与 parent wake 协调。") != NULL &&
                 strstr(output, "旧 output：快照层不该优先显示这句。") == NULL &&
                 raw_output &&
                 strstr(raw_output, "旧 output：快照层不该优先显示这句。") != NULL;
            if (!root) {
                const char *err = cJSON_GetErrorPtr();
                pr_info("  delegate_state_json parse error ptr: %s", err ? err : "<null>");
            } else if (!ok) {
                pr_info("  delegate_state_json fields desc=%s", description ? description : "<null>");
                pr_info("  delegate_state_json fields summary=%s", summary ? summary : "<null>");
                pr_info("  delegate_state_json fields output=%s", output ? output : "<null>");
                pr_info("  delegate_state_json fields raw_output=%s", raw_output ? raw_output : "<null>");
            }
            cJSON_Delete(root);
        } else {
            ok = 0;
        }
    }
    if (!ok) {
        pr_info("  delegate_state_json child-first diag: %s", payload ? payload : "<null>");
    }

    free(payload);
    report("delegate state json agent summary prefers child session rendered text", ok);
}

static void test_delegate_completion_turn_summarizes_cross_module_relationships(void)
{
    drain_inbound_bus_for_test();

    struct message msg;
    memset(&msg, 0, sizeof(msg));
    strscpy(msg.channel, CHAN_WEBSOCKET, sizeof(msg.channel));
    strscpy(msg.chat_id, "chat_delegate_relationships", sizeof(msg.chat_id));
    strscpy(msg.source, MSG_SOURCE_DELEGATE, sizeof(msg.source));
    msg.content = strdup(
        "Delegate coordinator completed. Summarize the finished subagent outputs for the user directly.\n\n"
        "Coordinator snapshot:\n"
        "{"
          "\"coordinator_id\":\"dc_relationships\","
          "\"status\":\"done\","
          "\"agents\":["
            "{"
              "\"task_id\":\"dt_kernel\","
              "\"subagent_type\":\"explore\","
              "\"description\":\"分析 kernel 目录结构\","
              "\"status\":\"done\","
              "\"output\":\"kernel 负责主循环、turn 执行链路和上下文恢复，是整个 agent 的执行内核。\""
            "},"
            "{"
              "\"task_id\":\"dt_tool\","
              "\"subagent_type\":\"explore\","
              "\"description\":\"分析 tool 目录结构\","
              "\"status\":\"done\","
              "\"output\":\"drivers/tool 负责工具协议、tool runtime、delegate_task 和工具总线封装，是内核调用外部能力的桥。\""
            "},"
            "{"
              "\"task_id\":\"dt_llm\","
              "\"subagent_type\":\"explore\","
              "\"description\":\"分析 llm 目录结构\","
              "\"status\":\"done\","
              "\"output\":\"drivers/llm 负责 provider 适配、payload 组装和模型回退，为 kernel 提供统一的大模型调用接口。\""
            "}"
          "]"
        "}");

    agent_turn_process_new_message(&msg);

    struct message out;
    memset(&out, 0, sizeof(out));
    int ok = message_bus_pop_outbound(&out, 1000) == 0 &&
             out.content &&
             strstr(out.content, "kernel") &&
             strstr(out.content, "drivers/tool") &&
             strstr(out.content, "drivers/llm") &&
             strstr(out.content, "执行内核") &&
             strstr(out.content, "工具") &&
             strstr(out.content, "模型") &&
             strstr(out.content, "关系") &&
             out.reasoning == NULL;
    if (!ok && out.content) {
        pr_info("  delegate_relationships output: %s", out.content);
    }
    free(out.content);
    free(out.reasoning);
    free(out.image_path);
    free(msg.content);

    report("delegate completion turn summarizes cross-module relationships", ok);
}

static void test_delegate_completion_turn_summarizes_explicit_scope_boundaries(void)
{
    drain_inbound_bus_for_test();

    struct message msg;
    memset(&msg, 0, sizeof(msg));
    strscpy(msg.channel, CHAN_WEBSOCKET, sizeof(msg.channel));
    strscpy(msg.chat_id, "chat_delegate_scope_boundaries", sizeof(msg.chat_id));
    strscpy(msg.source, MSG_SOURCE_DELEGATE, sizeof(msg.source));
    msg.content = strdup(
        "Delegate coordinator completed. Summarize the finished subagent outputs for the user directly.\n\n"
        "Coordinator snapshot:\n"
        "{"
          "\"coordinator_id\":\"dc_scope_boundaries\","
          "\"status\":\"done\","
          "\"agents\":["
            "{"
              "\"task_id\":\"dt_turn\","
              "\"subagent_type\":\"explore\","
              "\"description\":\"kernel/turn 区域深入分析\","
              "\"scope_path\":\"kernel/turn\","
              "\"scope_kind\":\"subsystem\","
              "\"analysis_focus\":\"turn_execution\","
              "\"status\":\"done\","
              "\"output\":\"kernel/turn 承担单回合执行主链，包含 prepare、decision、exec、finish 等阶段，是 tool-call 循环和最终回复生成的核心路径。\""
            "},"
            "{"
              "\"task_id\":\"dt_tooling\","
              "\"subagent_type\":\"explore\","
              "\"description\":\"kernel/tooling 区域深入分析\","
              "\"scope_path\":\"kernel/tooling\","
              "\"scope_kind\":\"subsystem\","
              "\"analysis_focus\":\"coordination\","
              "\"status\":\"done\","
              "\"output\":\"kernel/tooling 更偏协调与唤醒层，包含 delegate_parent_wake、tool_guard、auto_verify 等，用来承接后台子任务状态、工具治理和完成通知。\""
            "},"
            "{"
              "\"task_id\":\"dt_tool\","
              "\"subagent_type\":\"explore\","
              "\"description\":\"drivers/tool 区域深入分析\","
              "\"scope_path\":\"drivers/tool\","
              "\"scope_kind\":\"subsystem\","
              "\"analysis_focus\":\"tool_runtime\","
              "\"status\":\"done\","
              "\"output\":\"drivers/tool 是工具适配层，负责 tool runtime、delegate_task、files/terminal 等工具协议与运行时封装。\""
            "}"
          "]"
        "}");

    agent_turn_process_new_message(&msg);

    struct message out;
    memset(&out, 0, sizeof(out));
    int ok = message_bus_pop_outbound(&out, 1000) == 0 &&
             out.content &&
             strstr(out.content, "kernel/turn") &&
             strstr(out.content, "kernel/tooling") &&
             strstr(out.content, "drivers/tool") &&
             strstr(out.content, "执行主链") &&
             strstr(out.content, "协调与唤醒") &&
             strstr(out.content, "工具适配层") &&
             strstr(out.content, "职责边界") &&
             out.reasoning == NULL;
    if (!ok && out.content) {
        pr_info("  delegate_scope_boundaries output: %s", out.content);
    }
    free(out.content);
    free(out.reasoning);
    free(out.image_path);
    free(msg.content);

    report("delegate completion turn summarizes explicit scope boundaries", ok);
}

static void test_delegate_completion_outbound_does_not_rearm_parent_wake(void)
{
    reset_delegate_wake_test_env();

    int ok = delegate_task_store_start_coordinator("dc_delegate_outbound", "chat_delegate_outbound", "", "", "parallel") == 0;
    delegate_coordinator_record_t record;
    struct message msg;

    if (ok) {
        ok = delegate_task_store_start("dt_delegate_outbound",
                                       "dc_delegate_outbound",
                                       "delegate_sync_outbound",
                                       "explore",
                                       "",
                                       "delegate outbound",
                                       "prompt",
                                       "deepseek-v4-pro",
                                       "kernel/turn",
                                       "subsystem",
                                       "turn_execution",
                                       NULL) == 0;
    }
    if (ok) {
        ok = delegate_task_store_attach_task("dc_delegate_outbound", "dt_delegate_outbound") == 0;
    }
    if (ok) {
        ok = delegate_task_store_mark_parent_response_sent("chat_delegate_outbound") == 0;
    }
    if (ok) {
        ok = delegate_task_store_complete("dt_delegate_outbound", "done summary", "", false) == 0;
    }

    agent_loop_poll_delegate_coordinators();
    memset(&record, 0, sizeof(record));
    if (ok) {
        ok = delegate_task_store_snapshot_coordinator("dc_delegate_outbound", &record) == 0 &&
             record.completion_notified &&
             record.parent_resume_enqueued &&
             record.wake_state == DELEGATE_WAKE_COMPLETED;
    }

    memset(&msg, 0, sizeof(msg));
    if (ok) {
        ok = message_bus_pop_inbound(&msg, 1000) == 0 &&
             strcmp(agent_msg_source_or_default(&msg), MSG_SOURCE_DELEGATE) == 0;
    }

    if (ok) {
        channel_runtime_set_sender_override_for_test(test_channel_sender);
        err_t dispatch_err = channel_runtime_dispatch_outbound(&msg);
        ok = dispatch_err == 0;
    }
    channel_runtime_set_sender_override_for_test(NULL);
    free(msg.content);
    free(msg.reasoning);
    free(msg.image_path);

    memset(&record, 0, sizeof(record));
    if (ok) {
        ok = delegate_task_store_snapshot_coordinator("dc_delegate_outbound", &record) == 0 &&
             record.wake_state == DELEGATE_WAKE_COMPLETED &&
             record.parent_resume_enqueued &&
             record.completion_notified &&
             delegate_parent_wake_pending_count_for_test() == 0;
    }

    report("delegate completion outbound does not rearm parent wake", ok);
}

static void test_delegate_batch_background_returns_all_session_ids(void)
{
    char output[8192];
    memset(output, 0, sizeof(output));

    const char *input =
        "{"
        "\"tasks\":["
          "{"
            "\"description\":\"kernel\","
            "\"prompt\":\"分析 /home/wangergou/code/github/daima-agent/kernel 的目录结构和关键模块，说明入口与主链。\","
            "\"subagent_type\":\"explore\""
          "},"
          "{"
            "\"description\":\"tool\","
            "\"prompt\":\"分析 /home/wangergou/code/github/daima-agent/drivers/tool 的目录结构和关键模块，说明职责与代表性文件。\","
            "\"subagent_type\":\"explore\""
          "},"
          "{"
            "\"description\":\"llm\","
            "\"prompt\":\"分析 /home/wangergou/code/github/daima-agent/drivers/llm 的目录结构和关键模块，说明模型接入职责。\","
            "\"subagent_type\":\"explore\""
          "}"
        "]"
        "}";

    const struct tool *t = tool_delegate_definition();
    err_t err = t->execute(input, output, sizeof(output));
    int ok = (err == 0) &&
             strstr(output, "\"coordinator_id\":\"dc_") &&
             strstr(output, "\"agents\":[") &&
             strstr(output, "\"session_id\":\"delegate_sync_");

    int session_count = 0;
    const char *cursor = output;
    while ((cursor = strstr(cursor, "\"session_id\":\"delegate_sync_")) != NULL) {
        session_count++;
        cursor += strlen("\"session_id\":\"delegate_sync_");
    }
    ok = ok && session_count == 3;
    report("delegate batch returns distinct child session ids", ok);
}

static void test_delegate_batch_explicit_scope_children_prefer_local_overview(void)
{
    char start_output[8192];
    char poll_output[16384];
    char coordinator_id[32] = {0};
    memset(start_output, 0, sizeof(start_output));
    memset(poll_output, 0, sizeof(poll_output));

    const char *input =
        "{"
        "\"tasks\":["
          "{"
            "\"description\":\"kernel/turn 区域分析\","
            "\"prompt\":\"分析 /home/wangergou/code/github/daima-agent/kernel/turn 的目录结构和关键模块。只做代表性覆盖，不要穷举。总结直接子目录、核心文件、主要职责，以及下一步值得继续看的文件。\","
            "\"subagent_type\":\"explore\""
          "},"
          "{"
            "\"description\":\"kernel/tooling 区域分析\","
            "\"prompt\":\"分析 /home/wangergou/code/github/daima-agent/kernel/tooling 的目录结构和关键模块。只做代表性覆盖，不要穷举。总结直接子目录、核心文件、主要职责，以及下一步值得继续看的文件。\","
            "\"subagent_type\":\"explore\""
          "},"
          "{"
            "\"description\":\"drivers/tool 区域分析\","
            "\"prompt\":\"分析 /home/wangergou/code/github/daima-agent/drivers/tool 的目录结构和关键模块。只做代表性覆盖，不要穷举。总结直接子目录、核心文件、主要职责，以及下一步值得继续看的文件。\","
            "\"subagent_type\":\"explore\""
          "}"
        "]"
        "}";

    const struct tool *t = tool_delegate_definition();
    err_t err = t->execute(input, start_output, sizeof(start_output));
    int ok = (err == 0) && strstr(start_output, "\"coordinator_id\":\"dc_");

    if (ok) {
        const char *marker = strstr(start_output, "\"coordinator_id\":\"");
        if (marker) {
            marker += strlen("\"coordinator_id\":\"");
            int i = 0;
            while (marker[i] && marker[i] != '"' && i < (int)sizeof(coordinator_id) - 1) {
                coordinator_id[i] = marker[i];
                i++;
            }
            coordinator_id[i] = '\0';
        }
        ok = coordinator_id[0] != '\0';
    }

    if (ok) {
        for (int i = 0; i < 80; i++) {
            snprintf(poll_output, sizeof(poll_output), "{\"coordinator_id\":\"%s\"}", coordinator_id);
            err = t->execute(poll_output, poll_output, sizeof(poll_output));
            if (err == 0 && strstr(poll_output, "\"status\":\"done\"")) {
                break;
            }
            usleep(100000);
        }
        ok = err == 0 &&
             strstr(poll_output, "\"status\":\"done\"") &&
             strstr(poll_output, "核心文件") &&
             strstr(poll_output, "职责判断") &&
             !strstr(poll_output, "Protocol failed") &&
             !strstr(poll_output, "raw tool request");
    }

    if (!ok) {
        pr_info("  batch_explicit_scope_children output: %s", poll_output[0] ? poll_output : start_output);
    }
    report("delegate batch explicit-scope children prefer local overview", ok);
}

static void test_delegate_batch_explore_children_with_path_are_normalized(void)
{
    char start_output[8192];
    char poll_output[16384];
    char request_json[256];
    char coordinator_id[32] = {0};
    memset(start_output, 0, sizeof(start_output));
    memset(poll_output, 0, sizeof(poll_output));

    const char *input =
        "{"
        "\"tasks\":["
          "{"
            "\"description\":\"kernel/turn 区域分析\","
            "\"prompt\":\"请分析 /home/wangergou/code/github/daima-agent/kernel/turn，重点说明职责边界。\","
            "\"subagent_type\":\"explore\""
          "},"
          "{"
            "\"description\":\"kernel/tooling 区域分析\","
            "\"prompt\":\"请分析 /home/wangergou/code/github/daima-agent/kernel/tooling，重点说明职责边界。\","
            "\"subagent_type\":\"explore\""
          "},"
          "{"
            "\"description\":\"drivers/tool 区域分析\","
            "\"prompt\":\"请分析 /home/wangergou/code/github/daima-agent/drivers/tool，重点说明职责边界。\","
            "\"subagent_type\":\"explore\""
          "}"
        "]"
        "}";

    const struct tool *t = tool_delegate_definition();
    err_t err = t->execute(input, start_output, sizeof(start_output));
    int ok = (err == 0) && strstr(start_output, "\"coordinator_id\":\"dc_");

    if (ok) {
        const char *marker = strstr(start_output, "\"coordinator_id\":\"");
        if (marker) {
            marker += strlen("\"coordinator_id\":\"");
            int i = 0;
            while (marker[i] && marker[i] != '"' && i < (int)sizeof(coordinator_id) - 1) {
                coordinator_id[i] = marker[i];
                i++;
            }
            coordinator_id[i] = '\0';
        }
        ok = coordinator_id[0] != '\0';
    }

    if (ok) {
        for (int i = 0; i < 80; i++) {
            snprintf(request_json, sizeof(request_json), "{\"coordinator_id\":\"%s\"}", coordinator_id);
            err = t->execute(request_json, poll_output, sizeof(poll_output));
            if (err == 0 && strstr(poll_output, "\"status\":\"done\"")) {
                break;
            }
            usleep(100000);
        }
        ok = err == 0 &&
             strstr(poll_output, "\"status\":\"done\"") &&
             !strstr(poll_output, "Protocol failed") &&
             !strstr(poll_output, "raw tool request") &&
             strstr(poll_output, "核心文件");
    }

    if (!ok) {
        pr_info("  batch_path_normalized output: %s", poll_output[0] ? poll_output : start_output);
    }
    report("delegate batch explore children with path are normalized", ok);
}

static void test_model_fallback_primary_override_uses_matching_provider(void)
{
    llm_response_t resp;
    memset(&resp, 0, sizeof(resp));

    cJSON *messages = cJSON_CreateArray();
    cJSON *user = cJSON_CreateObject();
    cJSON_AddStringToObject(user, "role", "user");
    cJSON_AddStringToObject(user, "content", "Reply with OK");
    cJSON_AddItemToArray(messages, user);

    err_t err = model_fallback_chat_with_fallback("Return plain text OK.",
                                                  messages,
                                                  NULL,
                                                  "deepseek-v4-pro",
                                                  false,
                                                  &resp);
    int ok = (err == 0) && resp.text && strstr(resp.text, "OK");
    llm_response_free(&resp);
    cJSON_Delete(messages);
    report("model fallback uses provider-aware primary override", ok);
}

static void test_context_prompt_mentions_batch_delegate(void)
{
    char buf[32768];
    memset(buf, 0, sizeof(buf));
    err_t err = context_build_system_prompt(buf, sizeof(buf));
    int ok = (err == 0) &&
             strstr(buf, "delegate_task({tasks:[...]})") &&
             strstr(buf, "coordinator_id") &&
             strstr(buf, "不要连续发多个单独的 `delegate_task`") &&
             strstr(buf, "多个 subagent") &&
             strstr(buf, "不要先自己对这些子问题调用 `files` 做大范围摸底");
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
        "\"description\":\"scan kernel\","
        "\"prompt\":\"分析 /home/wangergou/code/github/daima-agent/kernel 的目录结构和关键模块，说明入口与主链。\","
        "\"run_in_background\":true"
        "}");

    strscpy(resp.calls[1].id, "call_b", sizeof(resp.calls[1].id));
    strscpy(resp.calls[1].name, "delegate_task", sizeof(resp.calls[1].name));
    resp.calls[1].input = strdup(
        "{"
        "\"subagent_type\":\"explore\","
        "\"description\":\"scan drivers/tool\","
        "\"prompt\":\"分析 /home/wangergou/code/github/daima-agent/drivers/tool 的目录结构和关键模块，说明职责与代表性文件。\","
        "\"run_in_background\":true"
        "}");

    strscpy(resp.calls[2].id, "call_c", sizeof(resp.calls[2].id));
    strscpy(resp.calls[2].name, "delegate_task", sizeof(resp.calls[2].name));
    resp.calls[2].input = strdup(
        "{"
        "\"subagent_type\":\"explore\","
        "\"description\":\"scan drivers/llm\","
        "\"prompt\":\"分析 /home/wangergou/code/github/daima-agent/drivers/llm 的目录结构和关键模块。\","
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
    cJSON *content = agent_turn_build_tool_results(&resp, &msg,
                                                   tool_bus_tools_json_for_channel(msg.channel),
                                                   tool_output, sizeof(tool_output), &stats);

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
        "\"description\":\"scan kernel\","
        "\"prompt\":\"分析 /home/wangergou/code/github/daima-agent/kernel 的目录结构和关键模块。\","
        "\"run_in_background\":true"
        "}");

    strscpy(resp.calls[1].id, "call_b", sizeof(resp.calls[1].id));
    strscpy(resp.calls[1].name, "delegate_task", sizeof(resp.calls[1].name));
    resp.calls[1].input = strdup(
        "{"
        "\"subagent_type\":\"explore\","
        "\"description\":\"scan drivers\","
        "\"prompt\":\"分析 /home/wangergou/code/github/daima-agent/drivers 的目录结构和关键模块。\","
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
    cJSON *content = agent_turn_build_tool_results(&resp, &msg,
                                                   tool_bus_tools_json_for_channel(msg.channel),
                                                   tool_output, sizeof(tool_output), &stats);

    int ok = content &&
             stats.background_delegate_started &&
             strstr(stats.background_delegate_reply, "coordinator_id=dc_");

    cJSON_Delete(content);
    free(resp.calls[0].input);
    free(resp.calls[1].input);
    free(msg.content);
    report("turn_exec marks background delegate started", ok);
}

static void test_turn_exec_merges_same_turn_broad_discovery_after_background_start(void)
{
    llm_response_t resp;
    memset(&resp, 0, sizeof(resp));
    resp.tool_use = true;
    resp.call_count = 3;

    strscpy(resp.calls[0].id, "call_a", sizeof(resp.calls[0].id));
    strscpy(resp.calls[0].name, "terminal", sizeof(resp.calls[0].name));
    resp.calls[0].input = strdup(
        "{"
        "\"command\":\"cd /home/wangergou/code/github/daima-agent && find kernel -maxdepth 2 -type f | head -40\""
        "}");

    strscpy(resp.calls[1].id, "call_b", sizeof(resp.calls[1].id));
    strscpy(resp.calls[1].name, "terminal", sizeof(resp.calls[1].name));
    resp.calls[1].input = strdup(
        "{"
        "\"command\":\"cd /home/wangergou/code/github/daima-agent && find drivers/tool -maxdepth 2 -type f | head -40\""
        "}");

    strscpy(resp.calls[2].id, "call_c", sizeof(resp.calls[2].id));
    strscpy(resp.calls[2].name, "terminal", sizeof(resp.calls[2].name));
    resp.calls[2].input = strdup(
        "{"
        "\"command\":\"cd /home/wangergou/code/github/daima-agent && find drivers/llm -maxdepth 2 -type f | head -40\""
        "}");

    struct message msg;
    memset(&msg, 0, sizeof(msg));
    strscpy(msg.channel, "websocket", sizeof(msg.channel));
    strscpy(msg.chat_id, "self_test_broad_discovery_dedupe", sizeof(msg.chat_id));
    strscpy(msg.source, "user", sizeof(msg.source));
    msg.intent = INTENT_INVESTIGATE;
    msg.content = strdup("帮我分析 /home/wangergou/code/github/daima-agent 的目录结构和关键模块，要求同时安排多个 subagent：分别分析 kernel、drivers/tool、drivers/llm，最后汇总。");

    char tool_output[8192];
    turn_exec_stats_t stats;
    memset(&stats, 0, sizeof(stats));
    cJSON *content = agent_turn_build_tool_results(&resp, &msg,
                                                   tool_bus_tools_json_for_channel(msg.channel),
                                                   tool_output, sizeof(tool_output), &stats);

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
             third_text && strstr(third_text, "\"status\":\"merged_into_batch\"") &&
             strstr(second_text, "\"coordinator_id\":\"dc_") &&
             strstr(third_text, "\"coordinator_id\":\"dc_") &&
             stats.background_delegate_started &&
             stats.background_delegate_coordinator_id[0];
    }

    cJSON_Delete(content);
    free(resp.calls[0].input);
    free(resp.calls[1].input);
    free(resp.calls[2].input);
    free(msg.content);
    report("turn_exec merges same-turn broad discovery after background start", ok);
}

static void test_turn_exec_rewrites_root_list_into_explicit_multi_scope_batch(void)
{
    llm_response_t resp;
    memset(&resp, 0, sizeof(resp));
    resp.tool_use = true;
    resp.call_count = 1;

    strscpy(resp.calls[0].id, "call_root_1", sizeof(resp.calls[0].id));
    strscpy(resp.calls[0].name, "files", sizeof(resp.calls[0].name));
    resp.calls[0].input = strdup("{\"action\":\"list\",\"path\":\"/home/wangergou/code/github/daima-agent\"}");

    struct message msg;
    memset(&msg, 0, sizeof(msg));
    strscpy(msg.channel, "websocket", sizeof(msg.channel));
    strscpy(msg.chat_id, "web_probe_root_multi", sizeof(msg.chat_id));
    strscpy(msg.source, "user", sizeof(msg.source));
    msg.content = strdup("帮我分析 /home/wangergou/code/github/daima-agent 的目录结构和关键模块，要求同时安排多个 subagent：分别分析 kernel、drivers/tool、drivers/llm，最后汇总");

    char tool_output[8192];
    turn_exec_stats_t stats;
    memset(&stats, 0, sizeof(stats));
    cJSON *content = agent_turn_build_tool_results(&resp, &msg,
                                                   tool_bus_tools_json_for_channel(msg.channel),
                                                   tool_output, sizeof(tool_output), &stats);

    int ok = 0;
    if (content && cJSON_IsArray(content) && cJSON_GetArraySize(content) == 1) {
        cJSON *first = cJSON_GetArrayItem(content, 0);
        const char *first_text = first ? cJSON_GetStringValue(cJSON_GetObjectItem(first, "content")) : NULL;
        cJSON *root = cJSON_Parse(tool_output);
        cJSON *agents = root ? cJSON_GetObjectItem(root, "agents") : NULL;
        int agent_count = agents && cJSON_IsArray(agents) ? cJSON_GetArraySize(agents) : 0;
        int has_kernel = 0;
        int has_tool = 0;
        int has_llm = 0;
        int has_drivers = 0;

        for (int i = 0; agents && i < agent_count; i++) {
            cJSON *agent = cJSON_GetArrayItem(agents, i);
            const char *desc = agent ? cJSON_GetStringValue(cJSON_GetObjectItem(agent, "description")) : NULL;
            if (!desc) {
                continue;
            }
            if (strstr(desc, "kernel")) has_kernel = 1;
            if (strstr(desc, "tool")) has_tool = 1;
            if (strstr(desc, "llm")) has_llm = 1;
            if (strcmp(desc, "分析 drivers 目录结构") == 0) has_drivers = 1;
        }
        ok = first_text &&
             strstr(first_text, "\"coordinator_id\":\"dc_") &&
             stats.background_delegate_started &&
             strstr(stats.background_delegate_reply, "coordinator_id=") &&
             agent_count == 3 &&
             has_kernel &&
             has_tool &&
             has_llm &&
             !has_drivers;
        cJSON_Delete(root);
    }

    cJSON_Delete(content);
    free(resp.calls[0].input);
    free(msg.content);
    report("turn_exec rewrites root list into explicit multi scope batch", ok);
}

static void test_turn_exec_rewrites_explicit_relative_scope_list_into_batch(void)
{
    llm_response_t resp;
    memset(&resp, 0, sizeof(resp));
    resp.tool_use = true;
    resp.call_count = 1;

    strscpy(resp.calls[0].id, "call_scope_1", sizeof(resp.calls[0].id));
    strscpy(resp.calls[0].name, "files", sizeof(resp.calls[0].name));
    resp.calls[0].input = strdup("{\"action\":\"list\",\"path\":\"/home/wangergou/code/github/daima-agent\"}");

    struct message msg;
    memset(&msg, 0, sizeof(msg));
    strscpy(msg.channel, "websocket", sizeof(msg.channel));
    strscpy(msg.chat_id, "web_probe_explicit_scope_multi", sizeof(msg.chat_id));
    strscpy(msg.source, "user", sizeof(msg.source));
    msg.content = strdup("帮我分析 /home/wangergou/code/github/daima-agent 的目录结构和关键模块，重点对比 kernel/turn、kernel/tooling、drivers/tool 这三个区域，要求同时安排多个 subagent，最后汇总它们的职责边界。");

    char tool_output[8192];
    turn_exec_stats_t stats;
    memset(&stats, 0, sizeof(stats));
    cJSON *content = agent_turn_build_tool_results(&resp, &msg,
                                                   tool_bus_tools_json_for_channel(msg.channel),
                                                   tool_output, sizeof(tool_output), &stats);

    int ok = 0;
    if (content && cJSON_IsArray(content) && cJSON_GetArraySize(content) == 1) {
        cJSON *first = cJSON_GetArrayItem(content, 0);
        const char *first_text = first ? cJSON_GetStringValue(cJSON_GetObjectItem(first, "content")) : NULL;
        cJSON *root = cJSON_Parse(tool_output);
        cJSON *agents = root ? cJSON_GetObjectItem(root, "agents") : NULL;
        int agent_count = agents && cJSON_IsArray(agents) ? cJSON_GetArraySize(agents) : 0;
        int has_kernel_turn = 0;
        int has_kernel_tooling = 0;
        int has_drivers_tool = 0;

        for (int i = 0; agents && i < agent_count; i++) {
            cJSON *agent = cJSON_GetArrayItem(agents, i);
            const char *desc = agent ? cJSON_GetStringValue(cJSON_GetObjectItem(agent, "description")) : NULL;
            if (!desc) {
                continue;
            }
            if (strstr(desc, "turn")) has_kernel_turn = 1;
            if (strstr(desc, "tooling")) has_kernel_tooling = 1;
            if (strstr(desc, "tool")) has_drivers_tool = 1;
        }
        ok = first_text &&
             strstr(first_text, "\"coordinator_id\":\"dc_") &&
             stats.background_delegate_started &&
             agent_count == 3 &&
             has_kernel_turn &&
             has_kernel_tooling &&
             has_drivers_tool;
        cJSON_Delete(root);
    }

    cJSON_Delete(content);
    free(resp.calls[0].input);
    free(msg.content);
    report("turn_exec rewrites explicit relative scope list into batch", ok);
}

static void test_delegate_empty_input_is_recoverable(void)
{
    int ok = !agent_tool_protocol_failure_should_stop("delegate_task", "{}", "delegate_task: missing required field 'subagent_type'", ERR_INVALID_ARG);
    report("delegate empty input is recoverable", ok);
}

static void test_delegate_empty_input_is_recoverable_noise(void)
{
    int ok = agent_tool_failure_is_recoverable_noise("delegate_task",
                                                     "{}",
                                                     "delegate_task: missing required field 'subagent_type'",
                                                     ERR_INVALID_ARG);
    report("delegate empty input is recoverable noise", ok);
}

static void test_delegate_schema_avoids_anyof(void)
{
    const struct tool *t = tool_delegate_definition();
    int ok = t && t->input_schema_json &&
             !strstr(t->input_schema_json, "\"anyOf\"") &&
             strstr(t->input_schema_json, "\"minProperties\":1") &&
             strstr(t->input_schema_json, "\"enum\":[\"explore\",\"librarian\",\"oracle\",\"implement\"]") &&
             strstr(t->input_schema_json, "\"required\":[\"description\",\"prompt\",\"subagent_type\"]") &&
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
        const char *target_path = root ? cJSON_GetStringValue(cJSON_GetObjectItem(root, "target_path")) : NULL;
        ok = root &&
             patched_tool_name && strcmp(patched_tool_name, "delegate_task") == 0 &&
             subagent_type && strcmp(subagent_type, "explore") == 0 &&
             target_path && strcmp(target_path, "/tmp/project/src") == 0 &&
             prompt && strstr(prompt, "/tmp/project/src") &&
             strstr(prompt, "bounded exploration request") &&
             strstr(prompt, "Do not exhaustively enumerate every subdirectory");
        cJSON_Delete(root);
    }

    free(call.input);
    free(msg.content);
    free(patched);
    report("discovery files rewrite to delegate explore", ok);
}

static void test_discovery_terminal_rewritten_to_delegate(void)
{
    llm_tool_call_t call;
    memset(&call, 0, sizeof(call));
    strscpy(call.id, "rewrite_terminal_1", sizeof(call.id));
    strscpy(call.name, "terminal", sizeof(call.name));
    call.input = strdup("{\"command\":\"cd /home/wangergou/code/github/daima-agent && find . -maxdepth 3 -type d | head -60\"}");

    struct message msg;
    memset(&msg, 0, sizeof(msg));
    strscpy(msg.channel, "websocket", sizeof(msg.channel));
    strscpy(msg.chat_id, "rewrite_terminal_chat", sizeof(msg.chat_id));
    strscpy(msg.source, "user", sizeof(msg.source));
    msg.intent = INTENT_INVESTIGATE;
    msg.content = strdup("帮我分析 /home/wangergou/code/github/daima-agent 的目录结构和关键模块，重点说明 kernel、drivers/tool、drivers/llm 之间的关系");

    char *patched = tool_invocation_context_patch_input(&call, &msg);
    const char *patched_tool_name = tool_invocation_context_patch_tool_name(&call, &msg);
    int ok = 0;
    if (patched) {
        cJSON *root = cJSON_Parse(patched);
        const char *subagent_type = root ? cJSON_GetStringValue(cJSON_GetObjectItem(root, "subagent_type")) : NULL;
        const char *target_path = root ? cJSON_GetStringValue(cJSON_GetObjectItem(root, "target_path")) : NULL;
        ok = root &&
             patched_tool_name && strcmp(patched_tool_name, "delegate_task") == 0 &&
             subagent_type && strcmp(subagent_type, "explore") == 0 &&
             target_path && strcmp(target_path, "/home/wangergou/code/github/daima-agent") == 0;
        cJSON_Delete(root);
    }

    free(call.input);
    free(msg.content);
    free(patched);
    report("discovery terminal rewrite to delegate explore", ok);
}

static void test_interview_structured_batch_preserved_in_files_rewrite(void)
{
    llm_tool_call_t call;
    memset(&call, 0, sizeof(call));
    strscpy(call.id, "rewrite_interview_files_1", sizeof(call.id));
    strscpy(call.name, "files", sizeof(call.name));
    call.input = strdup("{\"action\":\"list\",\"path\":\"/home/wangergou/code/github/daima-agent\"}");

    struct message msg;
    memset(&msg, 0, sizeof(msg));
    strscpy(msg.channel, "websocket", sizeof(msg.channel));
    strscpy(msg.chat_id, "rewrite_interview_files_chat", sizeof(msg.chat_id));
    strscpy(msg.source, "user", sizeof(msg.source));
    msg.intent = INTENT_IMPLEMENT;
    msg.content = strdup(
        "帮我改一下 /home/wangergou/code/github/daima-agent ，但我还没想好改哪个模块，你先直接开始。\n\n"
        "[Interview clarification asked]\n你准备先改哪个具体模块或目录？\n\n"
        "[Interview clarification answer]\n"
        "先不要改代码。请直接使用一次 delegate_task 批量委托，必须是 tasks 数组，包含 3 个子任务："
        "1) explore 分析 /home/wangergou/code/github/daima-agent/kernel/turn；"
        "2) explore 分析 /home/wangergou/code/github/daima-agent/drivers/tool；"
        "3) explore 验证 sudo 权限链路。第 3 个子任务必须带 preflight_tool，"
        "tool_name=terminal，input={\"command\":\"sudo ls /root\",\"workdir\":\"/home/wangergou/code/github/daima-agent\"}，"
        "continue_on_error=false。");

    char *patched = tool_invocation_context_patch_input(&call, &msg);
    const char *patched_tool_name = tool_invocation_context_patch_tool_name(&call, &msg);
    int ok = 0;
    if (patched) {
        cJSON *root = cJSON_Parse(patched);
        cJSON *tasks = root ? cJSON_GetObjectItem(root, "tasks") : NULL;
        cJSON *sudo_task = tasks && cJSON_IsArray(tasks) ? cJSON_GetArrayItem(tasks, 2) : NULL;
        cJSON *preflight = sudo_task ? cJSON_GetObjectItem(sudo_task, "preflight_tool") : NULL;
        const char *tool_name = preflight ? cJSON_GetStringValue(cJSON_GetObjectItem(preflight, "tool_name")) : NULL;
        cJSON *input = preflight ? cJSON_GetObjectItem(preflight, "input") : NULL;
        const char *command = input ? cJSON_GetStringValue(cJSON_GetObjectItem(input, "command")) : NULL;
        ok = root &&
             patched_tool_name && strcmp(patched_tool_name, "delegate_task") == 0 &&
             tasks && cJSON_IsArray(tasks) && cJSON_GetArraySize(tasks) == 3 &&
             tool_name && strcmp(tool_name, "terminal") == 0 &&
             command && strcmp(command, "sudo ls /root") == 0;
        cJSON_Delete(root);
    }

    free(call.input);
    free(msg.content);
    free(patched);
    report("interview structured batch preserved in files rewrite", ok);
}

static void test_interview_structured_batch_preserved_in_terminal_rewrite(void)
{
    llm_tool_call_t call;
    memset(&call, 0, sizeof(call));
    strscpy(call.id, "rewrite_interview_terminal_1", sizeof(call.id));
    strscpy(call.name, "terminal", sizeof(call.name));
    call.input = strdup("{\"command\":\"ls -la /home/wangergou/code/github/daima-agent/kernel/tooling/delegate/delegate_parent_wake.c\",\"workdir\":\"/home/wangergou/code/github/daima-agent\"}");

    struct message msg;
    memset(&msg, 0, sizeof(msg));
    strscpy(msg.channel, "websocket", sizeof(msg.channel));
    strscpy(msg.chat_id, "rewrite_interview_terminal_chat", sizeof(msg.chat_id));
    strscpy(msg.source, "user", sizeof(msg.source));
    msg.intent = INTENT_IMPLEMENT;
    msg.content = strdup(
        "帮我改一下 /home/wangergou/code/github/daima-agent ，但我还没想好改哪个模块，你先直接开始。\n\n"
        "[Interview clarification asked]\n你准备先改哪个具体模块或目录？\n\n"
        "[Interview clarification answer]\n"
        "先不要改代码。请直接使用一次 delegate_task 批量委托，必须是 tasks 数组，包含 3 个子任务："
        "1) explore 分析 /home/wangergou/code/github/daima-agent/kernel/turn；"
        "2) explore 分析 /home/wangergou/code/github/daima-agent/drivers/tool；"
        "3) explore 验证 sudo 权限链路。第 3 个子任务必须带 preflight_tool，"
        "tool_name=terminal，input={\"command\":\"sudo ls /root\",\"workdir\":\"/home/wangergou/code/github/daima-agent\"}，"
        "continue_on_error=false。");

    char *patched = tool_invocation_context_patch_input(&call, &msg);
    const char *patched_tool_name = tool_invocation_context_patch_tool_name(&call, &msg);
    int ok = 0;
    if (patched) {
        cJSON *root = cJSON_Parse(patched);
        cJSON *tasks = root ? cJSON_GetObjectItem(root, "tasks") : NULL;
        cJSON *sudo_task = tasks && cJSON_IsArray(tasks) ? cJSON_GetArrayItem(tasks, 2) : NULL;
        const char *description = sudo_task ? cJSON_GetStringValue(cJSON_GetObjectItem(sudo_task, "description")) : NULL;
        ok = root &&
             patched_tool_name && strcmp(patched_tool_name, "delegate_task") == 0 &&
             tasks && cJSON_IsArray(tasks) && cJSON_GetArraySize(tasks) == 3 &&
             description && strstr(description, "sudo");
        cJSON_Delete(root);
    }

    free(call.input);
    free(msg.content);
    free(patched);
    report("interview structured batch preserved in terminal rewrite", ok);
}

static void test_interview_structured_batch_overrides_direct_delegate_task(void)
{
    llm_tool_call_t call;
    memset(&call, 0, sizeof(call));
    strscpy(call.id, "rewrite_interview_delegate_1", sizeof(call.id));
    strscpy(call.name, "delegate_task", sizeof(call.name));
    call.input = strdup("{\"subagent_type\":\"explore\",\"description\":\"探索 daima-agent 项目全貌\",\"prompt\":\"请分析 /home/wangergou/code/github/daima-agent 的目录结构和关键模块\"}");

    struct message msg;
    memset(&msg, 0, sizeof(msg));
    strscpy(msg.channel, "websocket", sizeof(msg.channel));
    strscpy(msg.chat_id, "rewrite_interview_delegate_chat", sizeof(msg.chat_id));
    strscpy(msg.source, "user", sizeof(msg.source));
    msg.intent = INTENT_IMPLEMENT;
    msg.content = strdup(
        "帮我改一下 /home/wangergou/code/github/daima-agent ，但我还没想好改哪个模块，你先直接开始。\n\n"
        "[Interview clarification asked]\n你准备先改哪个具体模块或目录？\n\n"
        "[Interview clarification answer]\n"
        "先不要改代码。请直接使用一次 delegate_task 批量委托，必须是 tasks 数组，包含 3 个子任务："
        "1) explore 分析 /home/wangergou/code/github/daima-agent/kernel/turn；"
        "2) explore 分析 /home/wangergou/code/github/daima-agent/drivers/tool；"
        "3) explore 验证 sudo 权限链路。第 3 个子任务必须带 preflight_tool，"
        "tool_name=terminal，input={\"command\":\"sudo ls /root\",\"workdir\":\"/home/wangergou/code/github/daima-agent\"}，"
        "continue_on_error=false。");

    char *directive = strdup(
        "{"
        "\"tasks\":["
        "{\"description\":\"分析 kernel/turn\",\"subagent_type\":\"explore\",\"target_path\":\"/home/wangergou/code/github/daima-agent/kernel/turn\",\"prompt\":\"分析 kernel/turn\"},"
        "{\"description\":\"分析 drivers/tool\",\"subagent_type\":\"explore\",\"target_path\":\"/home/wangergou/code/github/daima-agent/drivers/tool\",\"prompt\":\"分析 drivers/tool\"},"
        "{\"description\":\"验证 sudo 权限链路\",\"subagent_type\":\"explore\",\"target_path\":\"/home/wangergou/code/github/daima-agent\",\"prompt\":\"验证 sudo 权限链路\","
        "\"preflight_tool\":{\"tool_name\":\"terminal\",\"input\":{\"command\":\"sudo ls /root\",\"workdir\":\"/home/wangergou/code/github/daima-agent\"},\"continue_on_error\":false}}"
        "]"
        "}");

    int stored = delegate_turn_directive_store(msg.chat_id, directive);
    char *patched = stored ? tool_invocation_context_patch_input(&call, &msg) : NULL;
    int ok = 0;
    if (patched) {
        cJSON *root = cJSON_Parse(patched);
        cJSON *tasks = root ? cJSON_GetObjectItem(root, "tasks") : NULL;
        cJSON *sudo_task = tasks && cJSON_IsArray(tasks) ? cJSON_GetArrayItem(tasks, 2) : NULL;
        cJSON *preflight = sudo_task ? cJSON_GetObjectItem(sudo_task, "preflight_tool") : NULL;
        const char *tool_name = preflight ? cJSON_GetStringValue(cJSON_GetObjectItem(preflight, "tool_name")) : NULL;
        ok = root &&
             tasks && cJSON_IsArray(tasks) && cJSON_GetArraySize(tasks) == 3 &&
             tool_name && strcmp(tool_name, "terminal") == 0;
        cJSON_Delete(root);
    }

    delegate_turn_directive_clear(msg.chat_id);
    free(directive);
    free(call.input);
    free(msg.content);
    free(patched);
    report("interview structured batch overrides direct delegate_task", ok);
}

static void test_interview_structured_batch_overrides_existing_delegate_batch(void)
{
    llm_tool_call_t call;
    memset(&call, 0, sizeof(call));
    strscpy(call.id, "rewrite_interview_delegate_existing_batch", sizeof(call.id));
    strscpy(call.name, "delegate_task", sizeof(call.name));
    call.input = strdup(
        "{"
        "\"tasks\":["
        "{\"description\":\"分析 kernel/turn\",\"subagent_type\":\"explore\",\"target_path\":\"/home/wangergou/code/github/daima-agent/kernel/turn\",\"prompt\":\"分析 kernel/turn\"},"
        "{\"description\":\"分析 drivers/tool\",\"subagent_type\":\"explore\",\"target_path\":\"/home/wangergou/code/github/daima-agent/drivers/tool\",\"prompt\":\"分析 drivers/tool\"},"
        "{\"description\":\"验证 sudo 权限链路\",\"subagent_type\":\"explore\",\"target_path\":\"/home/wangergou/code/github/daima-agent\",\"prompt\":\"验证 sudo 权限链路\"}"
        "]"
        "}");

    struct message msg;
    memset(&msg, 0, sizeof(msg));
    strscpy(msg.channel, "websocket", sizeof(msg.channel));
    strscpy(msg.chat_id, "rewrite_interview_delegate_existing_batch_chat", sizeof(msg.chat_id));
    strscpy(msg.source, "user", sizeof(msg.source));
    msg.intent = INTENT_IMPLEMENT;
    msg.content = strdup(
        "帮我改一下 /home/wangergou/code/github/daima-agent ，但我还没想好改哪个模块，你先直接开始。\n\n"
        "[Interview clarification asked]\n你准备先改哪个具体模块或目录？\n\n"
        "[Interview clarification answer]\n"
        "先不要改代码。请直接使用一次 delegate_task 批量委托，必须是 tasks 数组，包含 3 个子任务："
        "1) explore 分析 /home/wangergou/code/github/daima-agent/kernel/turn；"
        "2) explore 分析 /home/wangergou/code/github/daima-agent/drivers/tool；"
        "3) explore 验证 sudo 权限链路。第 3 个子任务必须带 preflight_tool，"
        "tool_name=terminal，input={\"command\":\"sudo ls /root\",\"workdir\":\"/home/wangergou/code/github/daima-agent\"}，"
        "continue_on_error=false。");

    char *directive = strdup(
        "{"
        "\"tasks\":["
        "{\"description\":\"分析 kernel/turn\",\"subagent_type\":\"explore\",\"target_path\":\"/home/wangergou/code/github/daima-agent/kernel/turn\",\"prompt\":\"分析 kernel/turn\"},"
        "{\"description\":\"分析 drivers/tool\",\"subagent_type\":\"explore\",\"target_path\":\"/home/wangergou/code/github/daima-agent/drivers/tool\",\"prompt\":\"分析 drivers/tool\"},"
        "{\"description\":\"验证 sudo 权限链路\",\"subagent_type\":\"explore\",\"target_path\":\"/home/wangergou/code/github/daima-agent\",\"prompt\":\"验证 sudo 权限链路\","
        "\"preflight_tool\":{\"tool_name\":\"terminal\",\"input\":{\"command\":\"sudo ls /root\",\"workdir\":\"/home/wangergou/code/github/daima-agent\"},\"continue_on_error\":false}}"
        "]"
        "}");

    int stored = delegate_turn_directive_store(msg.chat_id, directive);
    char *patched = stored ? tool_invocation_context_patch_input(&call, &msg) : NULL;
    int ok = 0;
    if (patched) {
        cJSON *root = cJSON_Parse(patched);
        cJSON *tasks = root ? cJSON_GetObjectItem(root, "tasks") : NULL;
        cJSON *sudo_task = tasks && cJSON_IsArray(tasks) ? cJSON_GetArrayItem(tasks, 2) : NULL;
        cJSON *preflight = sudo_task ? cJSON_GetObjectItem(sudo_task, "preflight_tool") : NULL;
        const char *tool_name = preflight ? cJSON_GetStringValue(cJSON_GetObjectItem(preflight, "tool_name")) : NULL;
        ok = root &&
             tasks && cJSON_IsArray(tasks) && cJSON_GetArraySize(tasks) == 3 &&
             tool_name && strcmp(tool_name, "terminal") == 0;
        cJSON_Delete(root);
    }

    delegate_turn_directive_clear(msg.chat_id);
    free(directive);
    free(call.input);
    free(msg.content);
    free(patched);
    report("interview structured batch overrides existing delegate batch", ok);
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
        const char *target_path = root ? cJSON_GetStringValue(cJSON_GetObjectItem(root, "target_path")) : NULL;
        ok = root &&
             patched_tool_name && strcmp(patched_tool_name, "delegate_task") == 0 &&
             subagent_type && strcmp(subagent_type, "explore") == 0 &&
             target_path && strcmp(target_path, "/tmp/project/libimp-samples") == 0 &&
             prompt && strstr(prompt, "/tmp/project/libimp-samples") &&
             strstr(prompt, "optimize for fast coverage and early stop");
        cJSON_Delete(root);
    }

    free(call.input);
    free(msg.content);
    free(patched);
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

    free(call.input);
    free(msg.content);
    free(patched);
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

    free(call.input);
    free(msg.content);
    free(patched);
    report("bounded discovery prompt carries early-stop marker", ok);
}

static void test_discovery_patch_skips_delegate_subagent_chat(void)
{
    llm_tool_call_t call;
    memset(&call, 0, sizeof(call));
    strscpy(call.id, "rewrite_skip_1", sizeof(call.id));
    strscpy(call.name, "files", sizeof(call.name));
    call.input = strdup("{\"action\":\"list\",\"path\":\"/tmp/project/src\"}");

    struct message msg;
    memset(&msg, 0, sizeof(msg));
    strscpy(msg.channel, "websocket", sizeof(msg.channel));
    strscpy(msg.chat_id, "delegate_sync_99", sizeof(msg.chat_id));
    strscpy(msg.source, "internal", sizeof(msg.source));
    msg.intent = INTENT_INVESTIGATE;
    msg.content = strdup("帮我看看这个仓库的目录结构和关键模块");

    char *patched = tool_invocation_context_patch_input(&call, &msg);
    const char *patched_tool = tool_invocation_context_patch_tool_name(&call, &msg);
    int ok = !patched && !patched_tool;

    free(call.input);
    free(msg.content);
    free(patched);
    report("broad discovery patch skips delegate subagent chat", ok);
}

static void test_discovery_patch_skips_internal_broad_discovery_message(void)
{
    llm_tool_call_t call;
    memset(&call, 0, sizeof(call));
    strscpy(call.id, "rewrite_skip_2", sizeof(call.id));
    strscpy(call.name, "files", sizeof(call.name));
    call.input = strdup("{\"action\":\"list\",\"path\":\"/tmp/project/src\"}");

    struct message msg;
    memset(&msg, 0, sizeof(msg));
    strscpy(msg.channel, "websocket", sizeof(msg.channel));
    strscpy(msg.chat_id, "web_probe_skip", sizeof(msg.chat_id));
    strscpy(msg.source, "internal", sizeof(msg.source));
    msg.intent = INTENT_INVESTIGATE;
    msg.content = strdup("Investigate this codebase area and return a concise discovery summary with concrete evidence. This is a bounded exploration request.");

    char *patched = tool_invocation_context_patch_input(&call, &msg);
    const char *patched_tool = tool_invocation_context_patch_tool_name(&call, &msg);
    int ok = !patched && !patched_tool;

    free(call.input);
    free(msg.content);
    free(patched);
    report("broad discovery patch skips internal discovery prompt", ok);
}

static void test_discovery_patch_skips_explicit_multi_subagent_request(void)
{
    llm_tool_call_t call;
    memset(&call, 0, sizeof(call));
    strscpy(call.id, "rewrite_skip_3", sizeof(call.id));
    strscpy(call.name, "files", sizeof(call.name));
    call.input = strdup("{\"action\":\"list\",\"path\":\"/tmp/project/src\"}");

    struct message msg;
    memset(&msg, 0, sizeof(msg));
    strscpy(msg.channel, "websocket", sizeof(msg.channel));
    strscpy(msg.chat_id, "web_probe_multi", sizeof(msg.chat_id));
    strscpy(msg.source, "user", sizeof(msg.source));
    msg.intent = INTENT_INVESTIGATE;
    msg.content = strdup("帮我分析 /tmp/project 的目录结构和关键模块，要求同时安排多个 subagent，分别分析 kernel、drivers、docs，最后汇总");

    char *patched = tool_invocation_context_patch_input(&call, &msg);
    const char *patched_tool = tool_invocation_context_patch_tool_name(&call, &msg);
    int ok = !patched && !patched_tool;

    free(call.input);
    free(msg.content);
    free(patched);
    report("broad discovery patch skips explicit multi subagent request", ok);
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

static void test_delegate_local_repo_overview_prefers_structured_target_path(void)
{
    char summary[4096];
    delegate_request_t req = {0};
    strscpy(req.subagent_type, "explore", sizeof(req.subagent_type));
    strscpy(req.description, "broad discovery", sizeof(req.description));
    strscpy(req.target_path, "/home/wangergou/code/github/daima-agent", sizeof(req.target_path));
    strscpy(req.prompt,
            "Investigate this codebase area and return a concise discovery summary with concrete evidence. "
            "Original user request: 帮我分析 /home/wangergou/code/github/daima-agent 的目录结构和关键模块。 "
            "Requested files tool call: action=search path=/home/wangergou/code/github/daima-agent pattern=main target=files",
            sizeof(req.prompt));

    int ok = tool_delegate_try_local_repo_overview(&req, summary, sizeof(summary)) &&
             strstr(summary, "kernel") &&
             strstr(summary, "drivers") &&
             !strstr(summary, "proc、root、var") &&
             !strstr(summary, "lib.usr-is-merged");
    report("delegate local repo overview prefers structured target path", ok);
}

static void test_delegate_local_repo_overview_does_not_refine_structured_repo_root_from_background(void)
{
    char summary[4096];
    delegate_request_t req = {0};
    strscpy(req.subagent_type, "explore", sizeof(req.subagent_type));
    strscpy(req.description, "分析 daima-agent 目录结构与关键模块", sizeof(req.description));
    strscpy(req.target_path, "/home/wangergou/code/github/daima-agent", sizeof(req.target_path));
    strscpy(req.prompt,
            "请详细分析 /home/wangergou/code/github/daima-agent 这个 C 语言项目的目录结构和关键模块。\n\n"
            "背景信息：\n"
            "- 这是一个 C11 + Kbuild 的单二进制 AI Agent 项目\n"
            "- 默认主链在 kernel/turn\n"
            "- 工具委托和多 subagent 相关能力在 drivers/tool\n"
            "- 目标是做整仓结构分析，不是只看单个子目录\n",
            sizeof(req.prompt));

    int ok = tool_delegate_try_local_repo_overview(&req, summary, sizeof(summary)) &&
             strstr(summary, "scripts") &&
             strstr(summary, "kernel") &&
             strstr(summary, "drivers") &&
             strstr(summary, "README.md") &&
             !strstr(summary, "tool_delegate.c") &&
             !strstr(summary, "tool_file_paths.c");
    if (!ok) {
        pr_info("  repo_overview structured-root summary: %s", summary);
    }
    report("delegate local repo overview keeps structured repo root over prompt background", ok);
}

static void test_delegate_local_repo_overview_keeps_repo_root_for_top_level_request(void)
{
    char summary[4096];
    delegate_request_t req = {0};
    strscpy(req.subagent_type, "explore", sizeof(req.subagent_type));
    strscpy(req.description, "分析 daima-agent 代码库结构", sizeof(req.description));
    strscpy(req.prompt,
            "请深入分析 /home/wangergou/code/github/daima-agent 的代码库结构和关键模块。\n\n"
            "这是一个 C11 + Kbuild 的单二进制 AI Agent 项目。请：\n"
            "1. 分析顶层目录结构和各目录职责\n"
            "2. 查看 Makefile、README.md、AGENTS.md 的作用\n"
            "3. 重点理解 kernel/turn 与 drivers/tool 的关系，但输出仍然要先给整仓结构总结\n",
            sizeof(req.prompt));

    int ok = tool_delegate_try_local_repo_overview(&req, summary, sizeof(summary)) &&
             strstr(summary, "scripts") &&
             strstr(summary, "kernel") &&
             strstr(summary, "drivers") &&
             strstr(summary, "README.md") &&
             !strstr(summary, "立即子目录：context、channel、time、tooling、turn、printk");
    if (!ok) {
        pr_info("  repo_overview top-level summary: %s", summary);
    }
    report("delegate local repo overview keeps repo root for top-level request", ok);
}

static void test_delegate_repo_root_overview_prefers_batch_expansion(void)
{
    delegate_request_t req = {0};
    strscpy(req.subagent_type, "explore", sizeof(req.subagent_type));
    strscpy(req.description, "分析 daima-agent 代码库结构", sizeof(req.description));
    strscpy(req.prompt,
            "请深入分析 /home/wangergou/code/github/daima-agent 的代码库结构和关键模块。"
            "先给顶层目录结构和各目录职责，再拆开看 kernel、drivers/tool、drivers/llm 的关键模块关系。",
            sizeof(req.prompt));

    int ok = tool_delegate_should_expand_repo_root_overview_batch(&req);
    report("delegate repo-root overview prefers coordinator batch expansion", ok);
}

static void test_delegate_task_single_repo_root_explore_expands_to_batch(void)
{
    char output[8192];
    memset(output, 0, sizeof(output));

    const char *input =
        "{"
        "\"subagent_type\":\"explore\","
        "\"description\":\"分析 daima-agent 代码库结构\","
        "\"prompt\":\"请深入分析 /home/wangergou/code/github/daima-agent 的代码库结构和关键模块。先给顶层目录结构和各目录职责，再拆开看 kernel、drivers/tool、drivers/llm 的关键模块关系。\""
        "}";

    const struct tool *t = tool_delegate_definition();
    err_t err = t->execute(input, output, sizeof(output));
    cJSON *root = cJSON_Parse(output);
    cJSON *agents = root ? cJSON_GetObjectItem(root, "agents") : NULL;
    int agent_count = agents && cJSON_IsArray(agents) ? cJSON_GetArraySize(agents) : 0;
    int has_kernel = 0;
    int has_tool = 0;
    int has_llm = 0;

    for (int i = 0; agents && i < agent_count; i++) {
        cJSON *agent = cJSON_GetArrayItem(agents, i);
        const char *desc = agent ? cJSON_GetStringValue(cJSON_GetObjectItem(agent, "description")) : NULL;
        if (!desc) {
            continue;
        }
        if (strstr(desc, "kernel")) has_kernel = 1;
        if (strstr(desc, "tool")) has_tool = 1;
        if (strstr(desc, "llm")) has_llm = 1;
    }

    int ok = (err == 0) &&
             root &&
             strstr(output, "\"coordinator_id\":\"dc_") &&
             agent_count == 3 &&
             has_kernel &&
             has_tool &&
             has_llm;
    cJSON_Delete(root);
    report("delegate task single repo-root explore expands to coordinator batch", ok);
}

static void test_delegate_task_runtime_style_repo_root_explore_expands_to_batch(void)
{
    char output[8192];
    memset(output, 0, sizeof(output));

    const char *input =
        "{"
        "\"subagent_type\":\"explore\","
        "\"description\":\"分析 daima-agent 代码库结构与模块关系\","
        "\"prompt\":\"请对 /home/wangergou/code/github/daima-agent 代码库进行全面的目录结构和关键模块分析。具体要求如下：\\n\\n"
        "1. 整体目录结构：列出主要目录和文件，说明它们的功能。\\n"
        "2. 关键模块分析：重点分析 kernel、drivers/tool、drivers/llm 三个目录。\\n"
        "3. 模块关系：说明 kernel、drivers/tool、drivers/llm 之间的关系与协作边界。\\n"
        "4. 输出方式：先给整仓结构，再给三个子系统的职责和关系。\""
        "}";

    const struct tool *t = tool_delegate_definition();
    err_t err = t->execute(input, output, sizeof(output));
    cJSON *root = cJSON_Parse(output);
    cJSON *agents = root ? cJSON_GetObjectItem(root, "agents") : NULL;
    const char *delivery = root ? cJSON_GetStringValue(cJSON_GetObjectItem(root, "delivery")) : NULL;
    int agent_count = agents && cJSON_IsArray(agents) ? cJSON_GetArraySize(agents) : 0;
    int ok = (err == 0) &&
             root &&
             strstr(output, "\"coordinator_id\":\"dc_") &&
             !delivery &&
             agent_count == 3;

    if (!ok) {
        pr_info("  runtime_style_repo_root output: %s", output);
    }
    cJSON_Delete(root);
    report("delegate task runtime-style repo-root explore expands to coordinator batch", ok);
}

static void test_delegate_task_logged_runtime_repo_root_explore_expands_to_batch(void)
{
    char output[8192];
    memset(output, 0, sizeof(output));

    const char *input =
        "{"
        "\"subagent_type\":\"explore\","
        "\"description\":\"分析 daima-agent 目录结构与关键模块关系\","
        "\"prompt\":\"分析 /home/wangergou/code/github/daima-agent 的目录结构和关键模块。\\n\\n"
        "重点：\\n"
        "1. 列出顶层目录结构（到 2-3 层深度即可）\\n"
        "2. 找出 kernel/、drivers/tool/、drivers/llm/ 三个区域的关键模块\\n"
        "3. 说明它们之间的关系与协作边界\\n"
        "4. 不要穷举所有文件，优先做结构化总结。\""
        "}";

    const struct tool *t = tool_delegate_definition();
    err_t err = t->execute(input, output, sizeof(output));
    cJSON *root = cJSON_Parse(output);
    cJSON *agents = root ? cJSON_GetObjectItem(root, "agents") : NULL;
    const char *delivery = root ? cJSON_GetStringValue(cJSON_GetObjectItem(root, "delivery")) : NULL;
    int agent_count = agents && cJSON_IsArray(agents) ? cJSON_GetArraySize(agents) : 0;
    int ok = (err == 0) &&
             root &&
             strstr(output, "\"coordinator_id\":\"dc_") &&
             !delivery &&
             agent_count == 3;

    if (!ok) {
        pr_info("  logged_runtime_repo_root output: %s", output);
    }
    cJSON_Delete(root);
    report("delegate task logged-runtime repo-root explore expands to coordinator batch", ok);
}

static void test_delegate_request_accepts_preflight_tool(void)
{
    char output[512];
    const struct tool *t = tool_delegate_definition();
    const char *input =
        "{"
        "\"subagent_type\":\"explore\","
        "\"description\":\"验证 preflight tool 解析\","
        "\"prompt\":\"分析 /home/wangergou/code/github/daima-agent/kernel/turn 的目录结构\","
        "\"preflight_tool\":{"
        "\"tool_name\":\"terminal\","
        "\"input\":{\"command\":\"sudo ls /root\",\"workdir\":\"/home/wangergou/code/github/daima-agent\"},"
        "\"continue_on_error\":false"
        "}"
        "}";

    err_t err = t->execute(input, output, sizeof(output));
    int ok = (err == 0) || (err == ERR_FAIL);
    report("delegate request accepts preflight tool", ok);
}

static void test_delegate_batch_accepts_child_preflight_tool(void)
{
    char output[8192];
    const struct tool *t = tool_delegate_definition();
    const char *input =
        "{"
        "\"tasks\":["
        "{"
        "\"subagent_type\":\"explore\","
        "\"description\":\"分析 kernel/turn\","
        "\"prompt\":\"分析 /home/wangergou/code/github/daima-agent/kernel/turn 的目录结构\""
        "},"
        "{"
        "\"subagent_type\":\"explore\","
        "\"description\":\"验证 sudo 阻塞链路\","
        "\"prompt\":\"解释权限阻塞原因\","
        "\"preflight_tool\":{"
        "\"tool_name\":\"terminal\","
        "\"input\":{\"command\":\"sudo ls /root\",\"workdir\":\"/home/wangergou/code/github/daima-agent\"},"
        "\"continue_on_error\":false"
        "}"
        "}"
        "]"
        "}";

    err_t err = t->execute(input, output, sizeof(output));
    cJSON *root = cJSON_Parse(output);
    cJSON *agents = root ? cJSON_GetObjectItem(root, "agents") : NULL;
    int ok = (err == 0) && root && agents && cJSON_IsArray(agents) && cJSON_GetArraySize(agents) == 2;
    cJSON_Delete(root);
    report("delegate batch accepts child preflight tool", ok);
}


static void test_delegate_task_repo_root_target_path_explore_expands_to_batch(void)
{
    char output[8192];
    memset(output, 0, sizeof(output));

    const char *input =
        "{"
        "\"subagent_type\":\"explore\","
        "\"description\":\"分析 daima-agent 代码库结构和关键模块关系\","
        "\"target_path\":\"/home/wangergou/code/github/daima-agent\","
        "\"prompt\":\"请分析 /home/wangergou/code/github/daima-agent 代码库的目录结构和关键模块，重点说明 kernel、drivers/tool、drivers/llm 之间的关系。"
        "\\n\\n请完成以下任务："
        "\\n1. 先给顶层目录结构概览"
        "\\n2. 再说明 kernel、drivers/tool、drivers/llm 的职责边界"
        "\\n3. 最后总结它们之间的协作关系"
        "\\n4. 不要穷举全部文件，只做代表性覆盖。\""
        "}";

    const struct tool *t = tool_delegate_definition();
    err_t err = t->execute(input, output, sizeof(output));
    cJSON *root = cJSON_Parse(output);
    cJSON *agents = root ? cJSON_GetObjectItem(root, "agents") : NULL;
    const char *delivery = root ? cJSON_GetStringValue(cJSON_GetObjectItem(root, "delivery")) : NULL;
    int agent_count = agents && cJSON_IsArray(agents) ? cJSON_GetArraySize(agents) : 0;
    int ok = (err == 0) &&
             root &&
             strstr(output, "\"coordinator_id\":\"dc_") &&
             !delivery &&
             agent_count == 3;

    if (!ok) {
        pr_info("  repo_root_target_path output: %s", output);
    }
    cJSON_Delete(root);
    report("delegate task repo-root target_path explore expands to coordinator batch", ok);
}

static void test_delegate_task_oh_my_openagent_explicit_scopes_expand_to_batch(void)
{
    char output[8192];
    memset(output, 0, sizeof(output));

    const char *input =
        "{"
        "\"subagent_type\":\"explore\","
        "\"description\":\"分析 oh-my-openagent 仓库结构和关键模块\","
        "\"prompt\":\"请分析 /home/wangergou/code/github/oh-my-openagent 的目录结构和关键模块。"
        "重点同时安排多个 subagent，分别分析 "
        "/home/wangergou/code/github/oh-my-openagent/packages、"
        "/home/wangergou/code/github/oh-my-openagent/docs、"
        "/home/wangergou/code/github/oh-my-openagent/assets，"
        "最后汇总它们的职责边界。\""
        "}";

    const struct tool *t = tool_delegate_definition();
    err_t err = t->execute(input, output, sizeof(output));
    cJSON *root = cJSON_Parse(output);
    cJSON *agents = root ? cJSON_GetObjectItem(root, "agents") : NULL;
    int agent_count = agents && cJSON_IsArray(agents) ? cJSON_GetArraySize(agents) : 0;
    int has_packages = 0;
    int has_docs = 0;
    int has_assets = 0;

    for (int i = 0; agents && i < agent_count; i++) {
        cJSON *agent = cJSON_GetArrayItem(agents, i);
        const char *desc = agent ? cJSON_GetStringValue(cJSON_GetObjectItem(agent, "description")) : NULL;
        if (!desc) {
            continue;
        }
        if (strstr(desc, "packages")) has_packages = 1;
        if (strstr(desc, "docs")) has_docs = 1;
        if (strstr(desc, "assets")) has_assets = 1;
    }

    int ok = (err == 0) &&
             root &&
             strstr(output, "\"coordinator_id\":\"dc_") &&
             agent_count == 3 &&
             has_packages &&
             has_docs &&
             has_assets;
    if (!ok) {
        pr_info("  oh_my_openagent_batch output: %s", output);
    }
    cJSON_Delete(root);
    report("delegate task oh-my-openagent explicit scopes expand to coordinator batch", ok);
}

static void test_delegate_task_opencode_explicit_scopes_expand_to_batch(void)
{
    char output[8192];
    char repo_root[PATH_MAX];
    char app_path[PATH_MAX];
    char cli_path[PATH_MAX];
    char session_ui_path[PATH_MAX];
    char input[4096];
    memset(output, 0, sizeof(output));
    memset(input, 0, sizeof(input));
    build_workspace_opencode_path(repo_root, sizeof(repo_root), NULL);
    build_workspace_opencode_path(app_path, sizeof(app_path), "packages/app");
    build_workspace_opencode_path(cli_path, sizeof(cli_path), "packages/cli");
    build_workspace_opencode_path(session_ui_path, sizeof(session_ui_path), "packages/session-ui");

    snprintf(input,
             sizeof(input),
             "{"
             "\"subagent_type\":\"explore\","
             "\"description\":\"分析 opencode monorepo 结构和关键模块\","
             "\"prompt\":\"请分析 opencode monorepo 的目录结构和关键模块。"
             "重点同时安排多个 subagent，分别分析 "
             "opencode/packages/app、"
             "opencode/packages/cli、"
             "opencode/packages/session-ui，"
             "最后汇总 session-first 相关职责边界。\""
             "}");

    const struct tool *t = tool_delegate_definition();
    err_t err = t->execute(input, output, sizeof(output));
    cJSON *root = cJSON_Parse(output);
    cJSON *agents = root ? cJSON_GetObjectItem(root, "agents") : NULL;
    int agent_count = agents && cJSON_IsArray(agents) ? cJSON_GetArraySize(agents) : 0;
    int has_app = 0;
    int has_cli = 0;
    int has_session_ui = 0;

    for (int i = 0; agents && i < agent_count; i++) {
        cJSON *agent = cJSON_GetArrayItem(agents, i);
        const char *desc = agent ? cJSON_GetStringValue(cJSON_GetObjectItem(agent, "description")) : NULL;
        if (!desc) {
            continue;
        }
        if (strstr(desc, "app")) has_app = 1;
        if (strstr(desc, "cli")) has_cli = 1;
        if (strstr(desc, "session-ui")) has_session_ui = 1;
    }

    int ok = (err == 0) &&
             root &&
             strstr(output, "\"coordinator_id\":\"dc_") &&
             agent_count == 3 &&
             has_app &&
             has_cli &&
             has_session_ui;
    if (!ok) {
        pr_info("  opencode_batch output: %s", output);
    }
    cJSON_Delete(root);
    report("delegate task opencode explicit scopes expand to coordinator batch", ok);
}

static void test_delegate_repo_root_explicit_scopes_override_deep_analysis_gate(void)
{
    delegate_request_t req = {0};
    strscpy(req.subagent_type, "explore", sizeof(req.subagent_type));
    strscpy(req.description, "分析 opencode monorepo 结构和关键模块", sizeof(req.description));
    strscpy(req.prompt,
            "请分析 opencode monorepo 的目录结构和关键模块。"
            "重点同时安排多个 subagent，分别分析 "
            "opencode/packages/app、"
            "opencode/packages/cli、"
            "opencode/packages/session-ui，"
            "最后汇总 session-first 相关职责边界。",
            sizeof(req.prompt));

    int ok = tool_delegate_should_expand_repo_root_overview_batch(&req);
    report("delegate repo-root explicit scopes override deep analysis gate", ok);
}

static void test_delegate_dsml_output_filter(void)
{
    int ok = tool_delegate_text_has_dsml_markup("<｜｜DSML｜｜tool_calls><｜｜DSML｜｜invoke name=\"files\">") &&
             !tool_delegate_text_has_dsml_markup("这是正常的结构分析总结，包含关键目录和入口文件。");
    report("delegate DSML output detector", ok);
}

static void test_delegate_safe_output_returns_protocol_failure_summary(void)
{
    char safe[1024];
    tool_delegate_build_safe_output_text(
        "<｜｜DSML｜｜tool_calls>\n<｜｜DSML｜｜invoke name=\"files\">",
        "这是正常的仓库分析总结，入口在 kernel/turn，调度核心在 tool_delegate。",
        false,
        false,
        safe,
        sizeof(safe));
    int ok = safe[0] &&
             strstr(safe, "delegate_task: subagent returned tool markup/transcript instead of protocol JSON") &&
             !strstr(safe, "<｜｜DSML｜｜");
    report("delegate safe output returns protocol failure summary", ok);
}

static void test_delegate_safe_output_rejects_transcript_markup(void)
{
    char safe[1024];
    tool_delegate_build_safe_output_text(
        "我先检查关键目录。\n\n<bash>\nfind /tmp/project -maxdepth 2\n</bash>\n\nFILE: /tmp/project/main.c",
        "最终结论：入口在 main.c，调度核心在 kernel/turn。",
        false,
        false,
        safe,
        sizeof(safe));
    int ok = safe[0] &&
             strstr(safe, "delegate_task: subagent returned tool markup/transcript instead of protocol JSON") &&
             !strstr(safe, "<bash>") &&
             !strstr(safe, "FILE:");
    report("delegate safe output rejects transcript markup", ok);
}

static void test_delegate_safe_output_includes_excerpt_for_non_json_text(void)
{
    char safe[1024];
    tool_delegate_build_safe_output_text(
        "这里有一段不是 JSON 的纯文本总结，但是最终协议没有遵守。",
        "",
        false,
        false,
        safe,
        sizeof(safe));
    int ok = safe[0] &&
             strstr(safe, "delegate_task: subagent returned non-JSON result after finalizer failed") &&
             strstr(safe, "Excerpt:") &&
             strstr(safe, "这里有一段不是 JSON 的纯文本总结");
    report("delegate safe output includes excerpt for non-json text", ok);
}

static void test_delegate_safe_output_prefers_reasoning_on_budget_exhausted(void)
{
    char safe[1024];
    tool_delegate_build_safe_output_text(
        "",
        "目录结构主要分为 init、kernel、drivers、ipc、fs/net/lib、arch、spiffs_data、docs。关键模块集中在 kernel/turn、kernel/router、drivers/tool、drivers/llm。",
        true,
        false,
        safe,
        sizeof(safe));
    int ok = safe[0] &&
             strstr(safe, "目录结构主要分为 init、kernel、drivers") &&
             !strstr(safe, "tool iteration budget exhausted");
    report("delegate safe output prefers reasoning on budget exhausted", ok);
}

static void test_delegate_safe_output_prefers_json_summary_on_budget_exhausted(void)
{
    char safe[1024];
    tool_delegate_build_safe_output_text(
        "{\"status\":\"done\",\"summary\":\"顶层目录包括 kernel、drivers、init、docs。关键主链在 kernel/turn 与 drivers/tool。\",\"evidence\":[\"kernel/turn/turn_run.c\",\"drivers/tool/tool_delegate.c\"],\"risks\":[],\"next_files\":[\"docs/ARCHITECTURE.md\"]}",
        "",
        true,
        false,
        safe,
        sizeof(safe));
    int ok = safe[0] &&
             strstr(safe, "顶层目录包括 kernel、drivers、init、docs") &&
             strstr(safe, "kernel/turn") &&
             !strstr(safe, "tool iteration budget exhausted");
    report("delegate safe output keeps json summary on budget exhausted", ok);
}

static void test_delegate_fast_local_json_accepts_valid_json(void)
{
    char summary[2048];
    int ok = tool_delegate_try_fast_local_json(
        "explore",
        "分析 drivers/llm 目录结构",
        "{\"status\":\"done\",\"summary\":\"drivers/llm 负责多 provider/chat payload 与模型回退。\",\"evidence\":[\"drivers/llm/model_fallback.c\",\"drivers/llm/llm_openai_payload.c\"],\"risks\":[\"provider 行为差异\"],\"next_files\":[\"drivers/llm/llm_proxy.h\"]}",
        summary,
        sizeof(summary));
    ok = ok &&
         strstr(summary, "drivers/llm 负责多 provider/chat payload") &&
         strstr(summary, "drivers/llm/model_fallback.c") &&
         strstr(summary, "Next files:");
    report("delegate fast local json accepts valid json", ok);
}

static void test_delegate_fast_local_json_wraps_safe_text(void)
{
    char summary[2048];
    int ok = tool_delegate_try_fast_local_json(
        "explore",
        "分析 kernel 目录结构",
        "kernel 顶层主要包含 turn、context、tooling、runtime、channel。主回合执行集中在 kernel/turn，协调与通知集中在 kernel/tooling 与 kernel/loop.c。",
        summary,
        sizeof(summary));
    ok = ok &&
         strstr(summary, "kernel 顶层主要包含 turn、context、tooling");
    report("delegate fast local json wraps safe text", ok);
}

static void test_delegate_fast_local_json_normalizes_terminal_result(void)
{
    char summary[2048];
    int ok = tool_delegate_try_fast_local_json(
        "explore",
        "执行 sudo ls /root 并解释权限原因",
        "{\"command\":\"sudo ls /root\",\"workdir\":\"/home/wangergou/code/github/daima-agent\",\"exit_code\":1,\"timed_out\":false,\"truncated\":false,\"output\":\"\",\"error\":\"sudo_password_cancelled\"}",
        summary,
        sizeof(summary));
    ok = ok &&
         strstr(summary, "sudo ls /root") &&
         strstr(summary, "sudo password") &&
         strstr(summary, "/root") &&
         !strstr(summary, "\"command\"");
    report("delegate fast local json normalizes terminal result", ok);
}

static void test_delegate_fast_local_json_rejects_files_tool_json(void)
{
    char summary[2048];
    memset(summary, 0, sizeof(summary));
    int ok = !tool_delegate_try_fast_local_json(
        "explore",
        "分析 kernel/turn 目录结构",
        "{\"action\":\"list\",\"path\":\"/home/wangergou/code/github/daima-agent/kernel/turn/\"}",
        summary,
        sizeof(summary));
    ok = ok && summary[0] == '\0';
    report("delegate fast local json rejects files tool json", ok);
}

static void test_delegate_local_repo_overview_shortcut_summarizes_kernel_dir(void)
{
    char summary[2048];
    delegate_request_t req = {0};
    strscpy(req.description, "分析 kernel 目录结构与关键模块", sizeof(req.description));
    strscpy(req.prompt, "分析 /home/wangergou/code/github/daima-agent/kernel 的目录结构和关键模块，说明入口与主链。", sizeof(req.prompt));
    int ok = tool_delegate_try_local_repo_overview(&req, summary, sizeof(summary));
    ok = ok &&
         strstr(summary, "立即子目录：") &&
         strstr(summary, "turn") &&
         strstr(summary, "建议继续看：");
    if (!ok) {
        pr_info("  repo_overview kernel summary: %s", summary);
    }
    report("delegate local repo overview shortcut summarizes kernel dir", ok);
}

static void test_delegate_local_repo_overview_shortcut_summarizes_explicit_subdir(void)
{
    char summary[2048];
    delegate_request_t req = {0};
    strscpy(req.description, "kernel/turn 区域深度分析", sizeof(req.description));
    strscpy(req.prompt,
            "分析 /home/wangergou/code/github/daima-agent/kernel/turn 的目录结构和关键模块。只做代表性覆盖，不要穷举。总结直接子目录、核心文件、主要职责，以及下一步值得继续看的文件。",
            sizeof(req.prompt));
    int ok = tool_delegate_try_local_repo_overview(&req, summary, sizeof(summary));
    ok = ok &&
         (strstr(summary, "立即子目录：") || strstr(summary, "核心文件：")) &&
         strstr(summary, "职责判断：") &&
         !strstr(summary, "{\"action\":\"list\"");
    if (!ok) {
        pr_info("  local_overview_explicit_subdir summary: %s", summary);
    }
    report("delegate local repo overview shortcut summarizes explicit subdir", ok);
}

static void test_delegate_local_repo_overview_accepts_target_path_focused_scope(void)
{
    char summary[2048];
    delegate_request_t req = {0};
    strscpy(req.subagent_type, "explore", sizeof(req.subagent_type));
    strscpy(req.description, "kernel/turn 区域深入分析", sizeof(req.description));
    strscpy(req.target_path, "/home/wangergou/code/github/daima-agent/kernel/turn", sizeof(req.target_path));
    strscpy(req.prompt,
            "请分析 /home/wangergou/code/github/daima-agent/kernel/turn，重点说明职责边界。",
            sizeof(req.prompt));
    int ok = tool_delegate_try_local_repo_overview(&req, summary, sizeof(summary));
    ok = ok &&
         strstr(summary, "核心文件：") &&
         strstr(summary, "职责判断：") &&
         !strstr(summary, "Protocol failed");
    if (!ok) {
        pr_info("  local_overview_target_path summary: %s", summary);
    }
    report("delegate local repo overview accepts target_path focused scope", ok);
}

static void test_delegate_local_repo_overview_filters_build_artifacts(void)
{
    char summary[2048];
    delegate_request_t req = {0};
    strscpy(req.description, "分析 drivers/llm 目录结构与关键模块", sizeof(req.description));
    strscpy(req.prompt, "分析 /home/wangergou/code/github/daima-agent/drivers/llm 的目录结构和关键模块。", sizeof(req.prompt));
    int ok = tool_delegate_try_local_repo_overview(&req, summary, sizeof(summary));
    ok = ok &&
         strstr(summary, "model_fallback.c") &&
         !strstr(summary, ".o") &&
         !strstr(summary, "立即子目录：Makefile");
    if (!ok) {
        pr_info("  repo_overview llm summary: %s", summary);
    }
    report("delegate local repo overview filters build artifacts", ok);
}

static void test_delegate_dependency_merge_shortcut_uses_conclusion_style(void)
{
    char summary[2048];
    char rendered[2048];
    delegate_request_t req = {0};
    delegate_preflight_tool_view_t preflight = {0};
    int ok;

    memset(summary, 0, sizeof(summary));
    memset(rendered, 0, sizeof(rendered));
    strscpy(req.subagent_type, "oracle", sizeof(req.subagent_type));
    strscpy(req.description, "汇总 turn 与 tooling 边界", sizeof(req.description));
    strscpy(req.prompt,
            "基于前面两个子任务的结果，汇总 kernel/turn 与 kernel/tooling 的职责边界、调用关系和下一步最值得继续看的文件。不要改代码。",
            sizeof(req.prompt));
    strscpy(req.depends_on, "scan-turn,scan-tooling", sizeof(req.depends_on));

    delegate_task_store_reset_for_test();
    ok = delegate_task_store_start_coordinator("dc_dep_merge",
                                               "chat_dep_merge",
                                               "tr_dep_merge",
                                               "delegate-team",
                                               "staged") == 0;
    ok = ok && delegate_task_store_plan("dt_dep_turn",
                                        "dc_dep_merge",
                                        "delegate_sync_dep_turn",
                                        "explore",
                                        "scan-turn",
                                        "分析 kernel/turn",
                                        "分析 kernel/turn",
                                        "deepseek-v4-pro",
                                        "/home/wangergou/code/github/daima-agent/kernel/turn",
                                        "subsystem",
                                        "turn_execution",
                                        "",
                                        &preflight) == 0;
    ok = ok && delegate_task_store_attach_task("dc_dep_merge", "dt_dep_turn") == 0;
    ok = ok && delegate_task_store_mark_running("dt_dep_turn") == 0;
    ok = ok && delegate_task_store_complete("dt_dep_turn",
                                            "kernel/turn 负责单回合执行主链、回合决策与最终回复生成。\n\nEvidence:\n- kernel/turn/turn_exec.c\n- kernel/turn/turn_entry.c",
                                            NULL,
                                            false) == 0;
    ok = ok && delegate_task_store_plan("dt_dep_tooling",
                                        "dc_dep_merge",
                                        "delegate_sync_dep_tooling",
                                        "explore",
                                        "scan-tooling",
                                        "分析 kernel/tooling",
                                        "分析 kernel/tooling",
                                        "deepseek-v4-pro",
                                        "/home/wangergou/code/github/daima-agent/kernel/tooling",
                                        "subsystem",
                                        "coordination",
                                        "",
                                        &preflight) == 0;
    ok = ok && delegate_task_store_attach_task("dc_dep_merge", "dt_dep_tooling") == 0;
    ok = ok && delegate_task_store_mark_running("dt_dep_tooling") == 0;
    ok = ok && delegate_task_store_complete("dt_dep_tooling",
                                            "kernel/tooling 负责工具治理、后台协调、parent wake 和执行期验证。\n\nEvidence:\n- kernel/tooling/delegate/delegate_parent_wake.c\n- kernel/tooling/delegate/delegate_task_store.c",
                                            NULL,
                                            false) == 0;

    ok = ok && tool_delegate_try_render_local_dependency_merge(&req,
                                                               "dc_dep_merge",
                                                               summary,
                                                               sizeof(summary));
    ok = ok &&
         tool_delegate_parse_result_json_rendered(summary, rendered, sizeof(rendered)) &&
         strstr(rendered, "职责边界：") != NULL &&
         strstr(rendered, "Next files:") != NULL &&
         strstr(rendered, "调用关系：") != NULL &&
         strstr(rendered, "kernel/turn") != NULL &&
         strstr(rendered, "kernel/tooling") != NULL &&
         strstr(summary, "Upstream findings:") == NULL &&
         strstr(summary, "本地汇总结论") == NULL &&
         strstr(summary, "Dependency result [") == NULL;
    if (!ok) {
        pr_info("  dependency_merge_shortcut raw=%s rendered=%s", summary, rendered);
    }
    report("delegate dependency merge shortcut uses conclusion style", ok);
}

static void test_delegate_dependency_merge_shortcut_renders_valid_result_json(void)
{
    char summary[2048];
    char rendered[2048];
    delegate_request_t req = {0};
    delegate_preflight_tool_view_t preflight = {0};
    int ok;

    memset(summary, 0, sizeof(summary));
    memset(rendered, 0, sizeof(rendered));
    strscpy(req.subagent_type, "oracle", sizeof(req.subagent_type));
    strscpy(req.description, "汇总 turn 与 tooling 边界", sizeof(req.description));
    strscpy(req.prompt,
            "基于前面两个子任务的结果，汇总 kernel/turn 与 kernel/tooling 的职责边界、调用关系和下一步最值得继续看的文件。不要改代码。",
            sizeof(req.prompt));
    strscpy(req.depends_on, "scan-turn,scan-tooling", sizeof(req.depends_on));

    delegate_task_store_reset_for_test();
    ok = delegate_task_store_start_coordinator("dc_dep_merge_json",
                                               "chat_dep_merge_json",
                                               "tr_dep_merge_json",
                                               "delegate-team",
                                               "staged") == 0;
    ok = ok && delegate_task_store_plan("dt_dep_turn_json",
                                        "dc_dep_merge_json",
                                        "delegate_sync_dep_turn_json",
                                        "explore",
                                        "scan-turn",
                                        "分析 kernel/turn",
                                        "分析 kernel/turn",
                                        "deepseek-v4-pro",
                                        "/home/wangergou/code/github/daima-agent/kernel/turn",
                                        "subsystem",
                                        "turn_execution",
                                        "",
                                        &preflight) == 0;
    ok = ok && delegate_task_store_attach_task("dc_dep_merge_json", "dt_dep_turn_json") == 0;
    ok = ok && delegate_task_store_mark_running("dt_dep_turn_json") == 0;
    ok = ok && delegate_task_store_complete("dt_dep_turn_json",
                                            "kernel/turn 负责单回合执行主链、回合决策与最终回复生成。\n\nEvidence:\n- kernel/turn/turn_exec.c\n- kernel/turn/turn_entry.c",
                                            NULL,
                                            false) == 0;
    ok = ok && delegate_task_store_plan("dt_dep_tooling_json",
                                        "dc_dep_merge_json",
                                        "delegate_sync_dep_tooling_json",
                                        "explore",
                                        "scan-tooling",
                                        "分析 kernel/tooling",
                                        "分析 kernel/tooling",
                                        "deepseek-v4-pro",
                                        "/home/wangergou/code/github/daima-agent/kernel/tooling",
                                        "subsystem",
                                        "coordination",
                                        "",
                                        &preflight) == 0;
    ok = ok && delegate_task_store_attach_task("dc_dep_merge_json", "dt_dep_tooling_json") == 0;
    ok = ok && delegate_task_store_mark_running("dt_dep_tooling_json") == 0;
    ok = ok && delegate_task_store_complete("dt_dep_tooling_json",
                                            "kernel/tooling 负责工具治理、后台协调、parent wake 和执行期验证。\n\nEvidence:\n- kernel/tooling/delegate/delegate_parent_wake.c\n- kernel/tooling/delegate/delegate_task_store.c",
                                            NULL,
                                            false) == 0;

    ok = ok && tool_delegate_try_render_local_dependency_merge(&req,
                                                               "dc_dep_merge_json",
                                                               summary,
                                                               sizeof(summary));
    ok = ok &&
         tool_delegate_result_json_has_nonempty_evidence(summary) &&
         tool_delegate_parse_result_json_rendered(summary, rendered, sizeof(rendered)) &&
         strstr(rendered, "职责边界：") != NULL &&
         strstr(rendered, "Evidence:") != NULL &&
         strstr(rendered, "Next files:") != NULL;
    if (!ok) {
        pr_info("  dependency_merge_shortcut_json raw=%s rendered=%s", summary, rendered);
    }
    report("delegate dependency merge shortcut renders valid result json", ok);
}

static void test_delegate_dependency_merge_shortcut_backfills_evidence_without_upstream_section(void)
{
    char summary[2048];
    char rendered[2048];
    delegate_request_t req = {0};
    delegate_preflight_tool_view_t preflight = {0};
    int ok;

    memset(summary, 0, sizeof(summary));
    memset(rendered, 0, sizeof(rendered));
    strscpy(req.subagent_type, "oracle", sizeof(req.subagent_type));
    strscpy(req.description, "汇总 turn 与 tooling 边界", sizeof(req.description));
    strscpy(req.prompt,
            "基于前面两个子任务的结果，汇总 kernel/turn 与 kernel/tooling 的职责边界、调用关系和下一步最值得继续看的文件。不要改代码。",
            sizeof(req.prompt));
    strscpy(req.depends_on, "scan-turn,scan-tooling", sizeof(req.depends_on));

    delegate_task_store_reset_for_test();
    ok = delegate_task_store_start_coordinator("dc_dep_merge_fallback",
                                               "chat_dep_merge_fallback",
                                               "tr_dep_merge_fallback",
                                               "delegate-team",
                                               "staged") == 0;
    ok = ok && delegate_task_store_plan("dt_dep_turn_fallback",
                                        "dc_dep_merge_fallback",
                                        "delegate_sync_dep_turn_fallback",
                                        "explore",
                                        "scan-turn",
                                        "分析 kernel/turn",
                                        "分析 kernel/turn",
                                        "deepseek-v4-pro",
                                        "/home/wangergou/code/github/daima-agent/kernel/turn",
                                        "subsystem",
                                        "turn_execution",
                                        "",
                                        &preflight) == 0;
    ok = ok && delegate_task_store_attach_task("dc_dep_merge_fallback", "dt_dep_turn_fallback") == 0;
    ok = ok && delegate_task_store_mark_running("dt_dep_turn_fallback") == 0;
    ok = ok && delegate_task_store_complete("dt_dep_turn_fallback",
                                            "kernel/turn 负责单回合执行主链、回合决策与最终回复生成。",
                                            NULL,
                                            false) == 0;
    ok = ok && delegate_task_store_plan("dt_dep_tooling_fallback",
                                        "dc_dep_merge_fallback",
                                        "delegate_sync_dep_tooling_fallback",
                                        "explore",
                                        "scan-tooling",
                                        "分析 kernel/tooling",
                                        "分析 kernel/tooling",
                                        "deepseek-v4-pro",
                                        "/home/wangergou/code/github/daima-agent/kernel/tooling",
                                        "subsystem",
                                        "coordination",
                                        "",
                                        &preflight) == 0;
    ok = ok && delegate_task_store_attach_task("dc_dep_merge_fallback", "dt_dep_tooling_fallback") == 0;
    ok = ok && delegate_task_store_mark_running("dt_dep_tooling_fallback") == 0;
    ok = ok && delegate_task_store_complete("dt_dep_tooling_fallback",
                                            "kernel/tooling 负责工具治理、后台协调、parent wake 和执行期验证。",
                                            NULL,
                                            false) == 0;

    ok = ok && tool_delegate_try_render_local_dependency_merge(&req,
                                                               "dc_dep_merge_fallback",
                                                               summary,
                                                               sizeof(summary));
    ok = ok &&
         tool_delegate_result_json_has_nonempty_evidence(summary) &&
         tool_delegate_parse_result_json_rendered(summary, rendered, sizeof(rendered)) &&
         strstr(rendered, "kernel/turn/turn_entry.c") != NULL &&
         strstr(rendered, "kernel/tooling/delegate/delegate_parent_wake.c") != NULL;
    if (!ok) {
        pr_info("  dependency_merge_shortcut_backfill raw=%s rendered=%s", summary, rendered);
    }
    report("delegate dependency merge shortcut backfills evidence without upstream section", ok);
}

static void test_delegate_local_repo_overview_accepts_backtick_wrapped_path(void)
{
    char summary[2048];
    delegate_request_t req = {0};
    strscpy(req.description, "分析 kernel 目录结构与关键模块", sizeof(req.description));
    strscpy(req.prompt, "分析 `/home/wangergou/code/github/daima-agent/kernel/` 的目录结构和关键模块，说明入口与主链。", sizeof(req.prompt));
    int ok = tool_delegate_try_local_repo_overview(&req, summary, sizeof(summary));
    ok = ok &&
         strstr(summary, "立即子目录：") &&
         strstr(summary, "turn") &&
         strstr(summary, "建议继续看：");
    if (!ok) {
        pr_info("  repo_overview backtick summary: %s", summary);
    }
    report("delegate local repo overview accepts backtick wrapped path", ok);
}

static void test_delegate_local_repo_overview_accepts_directory_tree_prompt(void)
{
    char summary[2048];
    delegate_request_t req = {0};
    strscpy(req.description, "Explore kernel directory", sizeof(req.description));
    strscpy(req.prompt, "对 /home/wangergou/code/github/daima-agent/kernel 目录做完整摸底。给出目录树、关键模块、代表性文件和主要职责。", sizeof(req.prompt));
    int ok = tool_delegate_try_local_repo_overview(&req, summary, sizeof(summary));
    ok = ok &&
         strstr(summary, "立即子目录：") &&
         strstr(summary, "turn") &&
         strstr(summary, "职责判断：");
    if (!ok) {
        pr_info("  repo_overview tree prompt summary: %s", summary);
    }
    report("delegate local repo overview accepts directory tree prompt", ok);
}

static void test_websocket_client_chat_id_roundtrip_accepts_32_char_id(void)
{
    const char *chat_id = "web_probe_final_multi_1782505221";
    int ok = ws_client_chat_id_roundtrip_for_test(chat_id);
    report("websocket client chat_id roundtrip accepts 32 char id", ok);
}

static void test_delegate_explore_prompt_mentions_real_files_search_protocol(void)
{
    const struct tool *t = tool_files_definition();
    int ok = t && t->input_schema_json &&
             strstr(t->input_schema_json, "\"pattern\"") &&
             strstr(t->input_schema_json, "\"target\"") &&
             strstr(t->input_schema_json, "\"file_glob\"") &&
             strstr(t->input_schema_json, "\"output_mode\"");
    report("files schema exposes real search protocol fields", ok);
}

static void test_delegate_result_json_parser_accepts_valid_json(void)
{
    char summary[1024];
    int ok = tool_delegate_parse_result_json_summary(
        "{\"status\":\"done\",\"summary\":\"kernel/loop.c drives the main loop.\",\"evidence\":[\"kernel/loop.c: agent loop\"],\"risks\":[\"No retry isolation\"],\"next_files\":[\"kernel/tooling/delegate/delegate_task_store.c\"]}",
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
         strstr(summary, "Subagent stopped before producing findings.") &&
         strstr(summary, "Returned only next-step narration") &&
         !strstr(summary, "protocol failure");
    report("delegate result json renderer accepts blocked json", ok);
}

static void test_delegate_all_subagents_prefer_structured_output(void)
{
    int ok = tool_delegate_subagent_prefers_structured_output(DELEGATE_SUBAGENT_EXPLORE) &&
             tool_delegate_subagent_prefers_structured_output(DELEGATE_SUBAGENT_LIBRARIAN) &&
             tool_delegate_subagent_prefers_structured_output(DELEGATE_SUBAGENT_ORACLE) &&
             tool_delegate_subagent_prefers_structured_output(DELEGATE_SUBAGENT_IMPLEMENT) &&
             !tool_delegate_subagent_prefers_structured_output(DELEGATE_SUBAGENT_INVALID);
    report("delegate all subagents prefer structured output", ok);
}

static void test_delegate_extract_sync_final_output_accepts_done_wrapper(void)
{
    char summary[1024];
    int ok = tool_delegate_extract_sync_final_output(
        "{\"status\":\"done\",\"delivery\":\"sync_final\",\"subagent_type\":\"explore\",\"output\":\"kernel/turn handles the tool loop and drivers/tool handles tool execution.\"}",
        summary,
        sizeof(summary));
    ok = ok &&
         strstr(summary, "kernel/turn handles the tool loop") &&
         !strstr(summary, "\"delivery\"");
    report("delegate sync wrapper extracts final output", ok);
}

static void test_delegate_extract_sync_final_output_rejects_blocked_wrapper(void)
{
    char summary[256];
    int ok = !tool_delegate_extract_sync_final_output(
        "{\"status\":\"blocked\",\"delivery\":\"sync_final\",\"output\":\"still investigating\"}",
        summary,
        sizeof(summary));
    report("delegate sync wrapper rejects blocked output", ok);
}

static void test_turn_exec_marks_sync_delegate_completed(void)
{
    turn_exec_stats_t stats;
    memset(&stats, 0, sizeof(stats));

    agent_turn_maybe_mark_sync_delegate_completed(
        &stats,
        "delegate_task",
        "{\"status\":\"done\",\"delivery\":\"sync_final\",\"output\":\"目录入口在 init，执行循环在 kernel/turn。\"}");

    int ok = stats.sync_delegate_completed &&
             strstr(stats.sync_delegate_reply, "执行循环在 kernel/turn");
    report("turn_exec marks sync delegate completed", ok);
}

static void test_delegate_tool_description_mentions_no_duplicate_and_batch(void)
{
    const struct tool *t = tool_delegate_definition();
    int ok = t && t->description &&
             strstr(t->description, "do not duplicate the same exploration yourself") &&
             strstr(t->description, "Batch mode is preferred over multiple sibling delegate_task calls") &&
             strstr(t->description, "subagent_type=explore before raw files traversal");
    report("delegate tool description pushes subagent-first behavior", ok);
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
        "",
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
        "",
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

static void test_delegate_prepare_overview_prompt_is_bounded(void)
{
    char prepared[READ_FILE_MAX_CHARS + 4096];
    bool disable_tools = false;
    const char *prompt =
        "分析 /home/wangergou/code/github/daima-agent/drivers/tool 目录的完整结构，列出所有子目录和关键文件。"
        "重点关注模块划分、关键模块和入口。";
    int ok = tool_delegate_prepare_subagent_prompt(
        "explore",
        "分析 drivers/tool 目录结构",
        "/home/wangergou/code/github/daima-agent/drivers/tool",
        prompt,
        prepared,
        sizeof(prepared),
        &disable_tools);
    ok = ok &&
         !disable_tools &&
         strstr(prepared, "Bounded explore override:") &&
         strstr(prepared, "Do not enumerate every subdirectory or every file.") &&
         strstr(prepared, "representative coverage, not exhaustive traversal") &&
         strstr(prepared, "Never guess fake roots like `/repo`, `/workspace`, `/project`, or `/data/workspace`.") &&
         strstr(prepared, "terminal.workdir");
    if (!ok) {
        pr_info("  overview prepared prompt: %s", prepared);
    }
    report("delegate overview prompt is rewritten to bounded exploration", ok);
}

static void test_delegate_batch_child_prompt_injects_target_path_contract(void)
{
    delegate_request_t child;
    memset(&child, 0, sizeof(child));
    strscpy(child.subagent_type, "explore", sizeof(child.subagent_type));
    strscpy(child.description, "分析 packages/app 模块", sizeof(child.description));
    strscpy(child.target_path,
            "/home/wangergou/.agent-data/spiffs_data/workspace/opencode/packages/app",
            sizeof(child.target_path));
    strscpy(child.prompt,
            "分析 packages/app 的目录结构和关键模块，最后汇总职责边界。",
            sizeof(child.prompt));

    tool_delegate_normalize_batch_child_request(&child);

    int ok = strstr(child.prompt, "Target: /home/wangergou/.agent-data/spiffs_data/workspace/opencode/packages/app") != NULL &&
             strstr(child.prompt, "Requested scope: 分析 packages/app 模块") != NULL &&
             strstr(child.prompt, "Never guess fake roots like `/repo`, `/workspace`, `/project`, or `/data/workspace`.") != NULL &&
             strstr(child.prompt, "Treat this absolute path as the primary working scope for file exploration.") != NULL;
    if (!ok) {
        pr_info("  normalized batch child prompt: %s", child.prompt);
    }
    report("delegate batch child prompt injects target path contract", ok);
}

static void test_delegate_explore_prompt_forbids_fake_repo_roots(void)
{
    const char *prompt = tool_delegate_subagent_prompt_prefix(DELEGATE_SUBAGENT_EXPLORE);
    int ok = prompt &&
             strstr(prompt, "Do not guess synthetic roots like `/repo`, `/workspace`, `/project`, `/data/workspace`") &&
             strstr(prompt, "Valid examples: `{\"action\":\"search\",\"path\":\"/absolute/repo/root\"") &&
             strstr(prompt, "structured `workdir` field instead of `cd ... && ...`");
    if (!ok) {
        pr_info("  explore subagent prompt: %s", prompt ? prompt : "(null)");
    }
    report("delegate explore prompt forbids fake repo roots", ok);
}

static void test_turn_interview_appends_answer_into_current_message(void)
{
    struct message msg;
    memset(&msg, 0, sizeof(msg));
    strscpy(msg.channel, CHAN_WEBSOCKET, sizeof(msg.channel));
    strscpy(msg.chat_id, "test_interview_append", sizeof(msg.chat_id));
    strscpy(msg.source, MSG_SOURCE_USER, sizeof(msg.source));
    msg.content = strdup("帮我实现一个功能");

    err_t err = agent_turn_append_interview_answer_for_test(
        &msg,
        "你想改哪个文件？\n验收方式是什么？",
        "改 kernel/turn/turn_interview.c，跑 self-test 验收。");

    int ok = (err == 0) &&
             msg.content &&
             strstr(msg.content, "帮我实现一个功能") &&
             strstr(msg.content, "[Interview clarification asked]") &&
             strstr(msg.content, "你想改哪个文件？") &&
             strstr(msg.content, "[Interview clarification answer]") &&
             strstr(msg.content, "改 kernel/turn/turn_interview.c");
    if (msg.content) {
        free(msg.content);
    }
    report("turn interview appends answer into current message", ok);
}

static void test_turn_interview_answer_requests_continue_turn(void)
{
    struct message msg;
    memset(&msg, 0, sizeof(msg));
    strscpy(msg.channel, CHAN_WEBSOCKET, sizeof(msg.channel));
    strscpy(msg.chat_id, "test_interview_continue", sizeof(msg.chat_id));
    strscpy(msg.source, MSG_SOURCE_USER, sizeof(msg.source));
    msg.intent = INTENT_IMPLEMENT;
    msg.content = strdup("帮我改一下 /home/wangergou/code/github/daima-agent ，但我还没想好改哪个模块，你先直接开始。");

    agent_turn_interview_result_t result = {0};
    err_t err = agent_turn_apply_interview_answer_for_test(
        &msg,
        "你准备先改哪个具体模块或目录？\n这次是要做架构分析、功能实现，还是问题修复？",
        "先只分析 kernel/turn 和 kernel/tooling 的职责边界，不做代码修改。",
        &result);
    int ok = (err == 0) &&
             result.handled &&
             result.continue_turn &&
             msg.content &&
             strstr(msg.content, "[Interview clarification answer]") &&
             strstr(msg.content, "kernel/turn 和 kernel/tooling");

    if (msg.content) {
        free(msg.content);
    }
    report("turn interview answer requests continue turn", ok);
}

static void test_turn_interview_answer_stores_delegate_directive(void)
{
    struct message msg;
    memset(&msg, 0, sizeof(msg));
    strscpy(msg.channel, CHAN_WEBSOCKET, sizeof(msg.channel));
    strscpy(msg.chat_id, "test_interview_directive", sizeof(msg.chat_id));
    strscpy(msg.source, MSG_SOURCE_USER, sizeof(msg.source));
    msg.intent = INTENT_IMPLEMENT;
    msg.content = strdup("帮我改一下 /home/wangergou/code/github/daima-agent ，但我还没想好改哪个模块，你先直接开始。");

    const char *answer =
        "先不要改代码。请直接使用一次 delegate_task 批量委托，必须是 tasks 数组，包含 3 个子任务："
        "1) explore 分析 /home/wangergou/code/github/daima-agent/kernel/turn；"
        "2) explore 分析 /home/wangergou/code/github/daima-agent/drivers/tool；"
        "3) explore 验证 sudo 权限链路。第 3 个子任务必须带 preflight_tool，"
        "tool_name=terminal，input={\"command\":\"sudo ls /root\",\"workdir\":\"/home/wangergou/code/github/daima-agent\"}，"
        "continue_on_error=false。";

    agent_turn_interview_result_t result = {0};
    err_t err = agent_turn_apply_interview_answer_for_test(
        &msg,
        "你准备先改哪个具体模块或目录？\n这次是要做架构分析、功能实现，还是问题修复？",
        answer,
        &result);

    char directive[4096];
    int ok = (err == 0) &&
             result.handled &&
             result.continue_turn &&
             delegate_turn_directive_load_copy(msg.chat_id, directive, sizeof(directive)) &&
             strstr(directive, "\"tasks\"") &&
             strstr(directive, "\"preflight_tool\"") &&
             strstr(directive, "\"sudo ls /root\"");

    delegate_turn_directive_clear(msg.chat_id);
    if (msg.content) {
        free(msg.content);
    }
    report("turn interview answer stores delegate directive", ok);
}

static void test_ws_interactive_reply_stores_delegate_directive(void)
{
    const char *chat_id = "test_ws_interactive_directive";
    const char *payload =
        "{"
        "\"type\":\"interactive_reply\","
        "\"chat_id\":\"test_ws_interactive_directive\","
        "\"request_type\":\"question_text\","
        "\"request_id\":\"question_123\","
        "\"value\":\"先拆范围\","
        "\"delegate_directive\":{"
        "\"tasks\":["
        "{"
        "\"description\":\"分析 kernel/turn\","
        "\"subagent_type\":\"explore\","
        "\"target_path\":\"/home/wangergou/code/github/daima-agent/kernel/turn\","
        "\"prompt\":\"分析 kernel/turn\""
        "}"
        "]"
        "}"
        "}";
    char directive[4096];
    int ok;

    delegate_turn_directive_clear(chat_id);
    ws_client_dispatch_text_frame_for_test(123, NULL, payload, time(NULL));
    ok = delegate_turn_directive_load_copy(chat_id, directive, sizeof(directive)) &&
         strstr(directive, "\"tasks\"") &&
         strstr(directive, "\"分析 kernel/turn\"");
    delegate_turn_directive_clear(chat_id);
    report("ws interactive reply stores delegate directive", ok);
}

static void test_prometheus_interview_specificity_gate(void)
{
    int vague = prometheus_message_is_specific_for_test(
        "帮我改一下 /home/wangergou/code/github/daima-agent ，但我还没想好改哪个模块，你先直接开始。");
    int specific = prometheus_message_is_specific_for_test(
        "修改 /home/wangergou/code/github/daima-agent/kernel/turn/turn_interview.c，补 question_text websocket 恢复链路，并用 self-test 验收。");

    int ok = (!vague) && specific;
    report("prometheus interview gate distinguishes vague implement request from scoped one", ok);
}

static void test_prometheus_force_interview_for_repo_wide_vague_implement(void)
{
    prometheus_state_t state;
    memset(&state, 0, sizeof(state));

    err_t err = prometheus_check_needs_interview(
        "帮我改一下 /home/wangergou/code/github/daima-agent ，但我还没想好改哪个模块，你先直接开始。",
        &state);

    int ok = (err == 0) &&
             state.enabled &&
             state.needs_interview &&
             strstr(state.questions, "具体模块") &&
             strstr(state.questions, "架构分析");
    report("prometheus forces interview for vague repo-wide implement request", ok);
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

static void test_delegate_task_id_poll_returns_json_wrapper(void)
{
    const struct tool *t = tool_delegate_definition();
    char output[1024];
    delegate_task_store_reset_for_test();
    memset(output, 0, sizeof(output));

    int ok = delegate_task_store_start("dt_resume_live",
                                       "",
                                       "delegate_sync_resume_live",
                                       "explore",
                                       "",
                                       "resume live task",
                                       "prompt",
                                       "deepseek-v4-pro",
                                       "kernel/turn",
                                       "subsystem",
                                       "turn_execution",
                                       NULL) == 0;
    if (ok) {
        err_t err = t->execute("{\"task_id\":\"dt_resume_live\"}", output, sizeof(output));
        ok = (err == 0) &&
             strstr(output, "\"task_id\":\"dt_resume_live\"") &&
             strstr(output, "\"session_id\":\"delegate_sync_resume_live\"") &&
             strstr(output, "\"status\":\"running\"") &&
             strstr(output, "\"subagent_type\":\"explore\"") &&
             strstr(output, "\"description\":\"resume live task\"") &&
             strstr(output, "\"model\":\"deepseek-v4-pro\"");
    }
    if (!ok) {
        pr_info("  delegate poll wrapper diag: output=%s", output);
    }
    report("delegate task_id poll returns json wrapper", ok);
}

static void test_delegate_public_header_hides_internal_helpers(void)
{
    char header[8192];
    FILE *f = fopen("/home/wangergou/code/github/daima-agent/drivers/tool/tool_delegate.h", "r");
    int ok = (f != NULL);
    size_t nread = 0;
    if (ok) {
        nread = fread(header, 1, sizeof(header) - 1, f);
        header[nread] = '\0';
        fclose(f);
    }
    if (ok) {
        ok = strstr(header, "tool_delegate_write_json_response(") == NULL &&
             strstr(header, "tool_delegate_persist_turn_session(") == NULL &&
             strstr(header, "tool_delegate_infer_scope_metadata(") == NULL;
    }
    if (!ok) {
        pr_info("  delegate public header diag: %s", ok ? "" : header);
    }
    report("delegate public header hides internal helpers", ok);
}

static void test_delegate_sync_file_hides_preflight_runner(void)
{
    char source[32768];
    FILE *f = fopen("/home/wangergou/code/github/daima-agent/drivers/tool/tool_delegate_sync.c", "r");
    int ok = (f != NULL);
    size_t nread = 0;
    if (ok) {
        nread = fread(source, 1, sizeof(source) - 1, f);
        source[nread] = '\0';
        fclose(f);
    }
    if (ok) {
        ok = strstr(source, "static err_t maybe_execute_preflight_tool(") == NULL;
    }
    if (!ok) {
        pr_info("  delegate sync preflight diag: %s", ok ? "" : source);
    }
    report("delegate sync file hides preflight runner", ok);
}

static void test_delegate_sync_file_hides_result_finalizer(void)
{
    char source[32768];
    FILE *f = fopen("/home/wangergou/code/github/daima-agent/drivers/tool/tool_delegate_sync.c", "r");
    int ok = (f != NULL);
    size_t nread = 0;
    if (ok) {
        nread = fread(source, 1, sizeof(source) - 1, f);
        source[nread] = '\0';
        fclose(f);
    }
    if (ok) {
        ok = strstr(source, "tool_delegate_parse_result_json_rendered(") == NULL &&
             strstr(source, "tool_delegate_finalize_result_json(") == NULL &&
             strstr(source, "tool_delegate_build_safe_output_text(") == NULL &&
             strstr(source, "tool_delegate_try_fast_local_json(") == NULL;
    }
    if (!ok) {
        pr_info("  delegate sync finalizer diag: %s", ok ? "" : source);
    }
    report("delegate sync file hides result finalizer", ok);
}

static void test_delegate_runtime_file_hides_background_worker_launch(void)
{
    char source[32768];
    FILE *f = fopen("/home/wangergou/code/github/daima-agent/drivers/tool/tool_delegate_runtime.c", "r");
    int ok = (f != NULL);
    size_t nread = 0;
    if (ok) {
        nread = fread(source, 1, sizeof(source) - 1, f);
        source[nread] = '\0';
        fclose(f);
    }
    if (ok) {
        ok = strstr(source, "static void background_subagent_task(void *arg)") == NULL &&
             strstr(source, "task_create(background_subagent_task") == NULL;
    }
    if (!ok) {
        pr_info("  delegate runtime worker diag: %s", ok ? "" : source);
    }
    report("delegate runtime file hides background worker launch", ok);
}

static void test_delegate_runtime_file_hides_batch_prepare_and_restore_scan(void)
{
    char source[32768];
    FILE *f = fopen("/home/wangergou/code/github/daima-agent/drivers/tool/tool_delegate_runtime.c", "r");
    int ok = (f != NULL);
    size_t nread = 0;
    if (ok) {
        nread = fread(source, 1, sizeof(source) - 1, f);
        source[nread] = '\0';
        fclose(f);
    }
    if (ok) {
        ok = strstr(source, "static void normalize_batch_child_request(delegate_request_t *child)") == NULL &&
             strstr(source, "static err_t launch_ready_background_subagents(") == NULL;
    }
    if (!ok) {
        pr_info("  delegate runtime orchestration diag: %s", ok ? "" : source);
    }
    report("delegate runtime file hides batch prepare and restore scan", ok);
}

static void test_delegate_runtime_file_hides_lifecycle_entry(void)
{
    char source[32768];
    FILE *f = fopen("/home/wangergou/code/github/daima-agent/drivers/tool/tool_delegate_runtime.c", "r");
    int ok = (f != NULL);
    size_t nread = 0;
    if (ok) {
        nread = fread(source, 1, sizeof(source) - 1, f);
        source[nread] = '\0';
        fclose(f);
    }
    if (ok) {
        ok = strstr(source, "err_t delegate_launch_ready_background_subagents_for_runtime(void)") == NULL;
    }
    if (!ok) {
        pr_info("  delegate runtime lifecycle-entry diag: %s", ok ? "" : source);
    }
    report("delegate runtime file hides lifecycle entry", ok);
}

static void test_delegate_lifecycle_file_owns_runtime_entry(void)
{
    char source[32768];
    FILE *f = fopen("/home/wangergou/code/github/daima-agent/drivers/tool/tool_delegate_lifecycle.c", "r");
    int ok = (f != NULL);
    size_t nread = 0;
    if (ok) {
        nread = fread(source, 1, sizeof(source) - 1, f);
        source[nread] = '\0';
        fclose(f);
    }
    if (ok) {
        ok = strstr(source, "err_t delegate_launch_ready_background_subagents_for_runtime(void)") != NULL;
    }
    if (!ok) {
        pr_info("  delegate lifecycle ownership diag: %s", ok ? "" : source);
    }
    report("delegate lifecycle file owns runtime entry", ok);
}

static void test_agent_loop_file_hides_delegate_lifecycle_steps(void)
{
    char source[16384];
    FILE *f = fopen("/home/wangergou/code/github/daima-agent/kernel/loop.c", "r");
    int ok = (f != NULL);
    size_t nread = 0;
    if (ok) {
        nread = fread(source, 1, sizeof(source) - 1, f);
        source[nread] = '\0';
        fclose(f);
    }
    if (ok) {
        ok = strstr(source, "delegate_parent_wake_poll();") == NULL &&
             strstr(source, "delegate_launch_ready_background_subagents_for_runtime();") == NULL;
    }
    if (!ok) {
        pr_info("  agent loop delegate-lifecycle diag: %s", ok ? "" : source);
    }
    report("agent loop file hides delegate lifecycle steps", ok);
}

static void test_delegate_lifecycle_file_owns_poll_entry(void)
{
    char source[32768];
    FILE *f = fopen("/home/wangergou/code/github/daima-agent/drivers/tool/tool_delegate_lifecycle.c", "r");
    int ok = (f != NULL);
    size_t nread = 0;
    if (ok) {
        nread = fread(source, 1, sizeof(source) - 1, f);
        source[nread] = '\0';
        fclose(f);
    }
    if (ok) {
        ok = strstr(source, "err_t delegate_lifecycle_poll_runtime(void)") != NULL;
    }
    if (!ok) {
        pr_info("  delegate lifecycle poll-entry diag: %s", ok ? "" : source);
    }
    report("delegate lifecycle file owns poll entry", ok);
}

static void test_delegate_lifecycle_file_owns_runtime_candidate_filter(void)
{
    char source[32768];
    FILE *f = fopen("/home/wangergou/code/github/daima-agent/drivers/tool/tool_delegate_lifecycle.c", "r");
    int ok = (f != NULL);
    size_t nread = 0;
    if (ok) {
        nread = fread(source, 1, sizeof(source) - 1, f);
        source[nread] = '\0';
        fclose(f);
    }
    if (ok) {
        ok = strstr(source, "static bool delegate_lifecycle_should_scan_coordinator(") != NULL;
    }
    if (!ok) {
        pr_info("  delegate lifecycle candidate-filter diag: %s", ok ? "" : source);
    }
    report("delegate lifecycle file owns runtime candidate filter", ok);
}

static void test_delegate_lifecycle_runtime_entry_hides_inline_candidate_filter(void)
{
    char source[32768];
    FILE *f = fopen("/home/wangergou/code/github/daima-agent/drivers/tool/tool_delegate_lifecycle.c", "r");
    int ok = (f != NULL);
    size_t nread = 0;
    if (ok) {
        nread = fread(source, 1, sizeof(source) - 1, f);
        source[nread] = '\0';
        fclose(f);
    }
    if (ok) {
        const char *fn = strstr(source, "err_t delegate_launch_ready_background_subagents_for_runtime(void)");
        ok = fn != NULL &&
             strstr(fn, "if (changed[i].queued_count <= 0)") == NULL &&
             strstr(fn, "strcmp(changed[i].status, \"done\")") == NULL &&
             strstr(fn, "strcmp(changed[i].status, \"failed\")") == NULL;
    }
    if (!ok) {
        pr_info("  delegate lifecycle inline-filter diag: %s", ok ? "" : source);
    }
    report("delegate lifecycle runtime entry hides inline candidate filter", ok);
}

static void test_delegate_parent_wake_file_owns_terminal_resume_gate(void)
{
    char source[65536];
    FILE *f = fopen("/home/wangergou/code/github/daima-agent/kernel/tooling/delegate/delegate_parent_wake.c", "r");
    int ok = (f != NULL);
    size_t nread = 0;
    if (ok) {
        nread = fread(source, 1, sizeof(source) - 1, f);
        source[nread] = '\0';
        fclose(f);
    }
    if (ok) {
        ok = strstr(source, "static bool should_defer_terminal_parent_resume(") != NULL;
    }
    if (!ok) {
        pr_info("  delegate parent wake terminal-gate diag: %s", ok ? "" : source);
    }
    report("delegate parent wake file owns terminal resume gate", ok);
}

static void test_delegate_parent_wake_flush_hides_inline_terminal_resume_gate(void)
{
    char source[65536];
    FILE *f = fopen("/home/wangergou/code/github/daima-agent/kernel/tooling/delegate/delegate_parent_wake.c", "r");
    int ok = (f != NULL);
    size_t nread = 0;
    if (ok) {
        nread = fread(source, 1, sizeof(source) - 1, f);
        source[nread] = '\0';
        fclose(f);
    }
    if (ok) {
        const char *fn = strstr(source, "static void flush_pending_snapshot(const delegate_parent_wake_entry_t *snapshot)");
        ok = fn != NULL &&
             strstr(fn, "parent_chat_has_pending_request(record.chat_id)") == NULL &&
             strstr(fn, "parent_chat_has_recent_activity_locked(record.chat_id, now_ms)") == NULL &&
             strstr(fn, "coordinator_terminal_resume_requires_reply(&record)") == NULL;
    }
    if (!ok) {
        pr_info("  delegate parent wake inline-terminal-gate diag: %s", ok ? "" : source);
    }
    report("delegate parent wake flush hides inline terminal resume gate", ok);
}

static void test_delegate_parent_wake_file_uses_consumed_resume_gate(void)
{
    char source[32768];
    FILE *f = fopen("/home/wangergou/code/github/daima-agent/kernel/tooling/delegate/delegate_parent_wake.c", "r");
    int ok = (f != NULL);
    size_t nread = 0;
    if (ok) {
        nread = fread(source, 1, sizeof(source) - 1, f);
        source[nread] = '\0';
        fclose(f);
    }
    if (ok) {
        ok = strstr(source, "parent_chat_consumed_delegate_resume(") != NULL &&
             strstr(source, "turn_context_load_copy(") != NULL;
    }
    if (!ok) {
        pr_info("  delegate parent wake consumed-gate diag: %s", ok ? "" : source);
    }
    report("delegate parent wake file uses consumed resume gate", ok);
}

static void test_delegate_parent_wake_file_owns_resume_pending_derivation(void)
{
    char source[65536];
    FILE *f = fopen("/home/wangergou/code/github/daima-agent/kernel/tooling/delegate/delegate_parent_wake.c", "r");
    int ok = (f != NULL);
    size_t nread = 0;
    if (ok) {
        nread = fread(source, 1, sizeof(source) - 1, f);
        source[nread] = '\0';
        fclose(f);
    }
    if (ok) {
        ok = strstr(source, "static bool coordinator_has_resume_pending(") != NULL;
    }
    if (!ok) {
        pr_info("  delegate parent wake resume-pending diag: %s", ok ? "" : source);
    }
    report("delegate parent wake file owns resume pending derivation", ok);
}

static void test_delegate_parent_wake_hides_inline_resume_pending_derivation(void)
{
    char source[65536];
    FILE *f = fopen("/home/wangergou/code/github/daima-agent/kernel/tooling/delegate/delegate_parent_wake.c", "r");
    int ok = (f != NULL);
    size_t nread = 0;
    if (ok) {
        nread = fread(source, 1, sizeof(source) - 1, f);
        source[nread] = '\0';
        fclose(f);
    }
    if (ok) {
        ok = strstr(source, "resume_pending = coordinator_is_terminal(record) &&") == NULL &&
             strstr(source, "resume_pending = terminal &&") == NULL;
    }
    if (!ok) {
        pr_info("  delegate parent wake inline-resume-pending diag: %s", ok ? "" : source);
    }
    report("delegate parent wake hides inline resume pending derivation", ok);
}

static void test_delegate_parent_wake_file_owns_resume_state_helpers(void)
{
    char source[65536];
    FILE *f = fopen("/home/wangergou/code/github/daima-agent/kernel/tooling/delegate/delegate_parent_wake.c", "r");
    int ok = (f != NULL);
    size_t nread = 0;
    if (ok) {
        nread = fread(source, 1, sizeof(source) - 1, f);
        source[nread] = '\0';
        fclose(f);
    }
    if (ok) {
        ok = strstr(source, "static void retain_pending_resume_locked(") != NULL &&
             strstr(source, "static void defer_pending_resume_locked(") != NULL &&
             strstr(source, "static void drop_pending_resume_locked(") != NULL;
    }
    if (!ok) {
        pr_info("  delegate parent wake resume-state-helper diag: %s", ok ? "" : source);
    }
    report("delegate parent wake file owns resume state helpers", ok);
}

static void test_delegate_parent_wake_flush_hides_inline_resume_state_updates(void)
{
    char source[65536];
    FILE *f = fopen("/home/wangergou/code/github/daima-agent/kernel/tooling/delegate/delegate_parent_wake.c", "r");
    int ok = (f != NULL);
    size_t nread = 0;
    if (ok) {
        nread = fread(source, 1, sizeof(source) - 1, f);
        source[nread] = '\0';
        fclose(f);
    }
    if (ok) {
        const char *fn = strstr(source, "static void flush_pending_snapshot(const delegate_parent_wake_entry_t *snapshot)");
        const char *fn_end = fn ? strstr(fn, "\nerr_t delegate_parent_wake_init(void)") : NULL;
        char body[32768];
        if (!fn || !fn_end || fn_end <= fn) {
            ok = 0;
        } else {
            size_t body_len = (size_t)(fn_end - fn);
            if (body_len >= sizeof(body)) {
                body_len = sizeof(body) - 1;
            }
            memcpy(body, fn, body_len);
            body[body_len] = '\0';
            ok = strstr(body, "retain_pending_resume_locked(") != NULL &&
                 strstr(body, "defer_pending_resume_locked(") != NULL &&
                 strstr(body, "drop_pending_resume_locked(") != NULL &&
                 strstr(body, "s_pending[idx].resume_pending = true;") == NULL &&
                 strstr(body, "s_pending[idx].resume_pending = false;") == NULL &&
                 strstr(body, "s_pending[idx].resume_deferred_at_ms") == NULL &&
                 strstr(body, "s_pending[idx].retry_after_ms = retry_after_ms;") == NULL;
        }
    }
    if (!ok) {
        pr_info("  delegate parent wake inline-resume-state diag: %s", ok ? "" : source);
    }
    report("delegate parent wake flush hides inline resume state updates", ok);
}

static void test_delegate_parent_wake_file_owns_dispatch_helpers(void)
{
    char source[65536];
    FILE *f = fopen("/home/wangergou/code/github/daima-agent/kernel/tooling/delegate/delegate_parent_wake.c", "r");
    int ok = (f != NULL);
    size_t nread = 0;
    if (ok) {
        nread = fread(source, 1, sizeof(source) - 1, f);
        source[nread] = '\0';
        fclose(f);
    }
    if (ok) {
        ok = strstr(source, "static err_t dispatch_coordinator_snapshot(") != NULL &&
             strstr(source, "static err_t dispatch_terminal_completion(") != NULL;
    }
    if (!ok) {
        pr_info("  delegate parent wake dispatch-helper diag: %s", ok ? "" : source);
    }
    report("delegate parent wake file owns dispatch helpers", ok);
}

static void test_delegate_parent_wake_flush_hides_inline_sender_calls(void)
{
    char source[65536];
    FILE *f = fopen("/home/wangergou/code/github/daima-agent/kernel/tooling/delegate/delegate_parent_wake.c", "r");
    int ok = (f != NULL);
    size_t nread = 0;
    if (ok) {
        nread = fread(source, 1, sizeof(source) - 1, f);
        source[nread] = '\0';
        fclose(f);
    }
    if (ok) {
        const char *fn = strstr(source, "static void flush_pending_snapshot(const delegate_parent_wake_entry_t *snapshot)");
        const char *fn_end = fn ? strstr(fn, "\nerr_t delegate_parent_wake_init(void)") : NULL;
        char body[32768];
        if (!fn || !fn_end || fn_end <= fn) {
            ok = 0;
        } else {
            size_t body_len = (size_t)(fn_end - fn);
            if (body_len >= sizeof(body)) {
                body_len = sizeof(body) - 1;
            }
            memcpy(body, fn, body_len);
            body[body_len] = '\0';
            ok = strstr(body, "s_status_sender(") == NULL &&
                 strstr(body, "s_output_sender(") == NULL &&
                 strstr(body, "s_done_sender(") == NULL &&
                 strstr(body, "dispatch_visible_coordinator_update(") != NULL &&
                 strstr(body, "dispatch_terminal_completion(") != NULL;
        }
    }
    if (!ok) {
        pr_info("  delegate parent wake inline-sender diag: %s", ok ? "" : source);
    }
    report("delegate parent wake flush hides inline sender calls", ok);
}

static void test_delegate_parent_wake_file_owns_visible_update_helper(void)
{
    char source[65536];
    FILE *f = fopen("/home/wangergou/code/github/daima-agent/kernel/tooling/delegate/delegate_parent_wake.c", "r");
    int ok = (f != NULL);
    size_t nread = 0;
    if (ok) {
        nread = fread(source, 1, sizeof(source) - 1, f);
        source[nread] = '\0';
        fclose(f);
    }
    if (ok) {
        ok = strstr(source, "static err_t dispatch_visible_coordinator_update(") != NULL;
    }
    if (!ok) {
        pr_info("  delegate parent wake visible-update-helper diag: %s", ok ? "" : source);
    }
    report("delegate parent wake file owns visible update helper", ok);
}

static void test_delegate_parent_wake_flush_hides_inline_visible_update_sequence(void)
{
    char source[65536];
    FILE *f = fopen("/home/wangergou/code/github/daima-agent/kernel/tooling/delegate/delegate_parent_wake.c", "r");
    int ok = (f != NULL);
    size_t nread = 0;
    if (ok) {
        nread = fread(source, 1, sizeof(source) - 1, f);
        source[nread] = '\0';
        fclose(f);
    }
    if (ok) {
        const char *fn = strstr(source, "static void flush_pending_snapshot(const delegate_parent_wake_entry_t *snapshot)");
        const char *fn_end = fn ? strstr(fn, "\nerr_t delegate_parent_wake_init(void)") : NULL;
        char body[32768];
        if (!fn || !fn_end || fn_end <= fn) {
            ok = 0;
        } else {
            size_t body_len = (size_t)(fn_end - fn);
            if (body_len >= sizeof(body)) {
                body_len = sizeof(body) - 1;
            }
            memcpy(body, fn, body_len);
            body[body_len] = '\0';
            ok = strstr(body, "send_subagent_progress_events(&record);") == NULL &&
                 strstr(body, "delegate_task_store_mark_visible_revision_sent(") == NULL &&
                 strstr(body, "dispatch_visible_coordinator_update(") != NULL;
        }
    }
    if (!ok) {
        pr_info("  delegate parent wake inline-visible-update diag: %s", ok ? "" : source);
    }
    report("delegate parent wake flush hides inline visible update sequence", ok);
}

static void test_delegate_protocol_file_hides_markup_sanitizer(void)
{
    char source[32768];
    FILE *f = fopen("/home/wangergou/code/github/daima-agent/drivers/tool/tool_delegate_protocol.c", "r");
    int ok = (f != NULL);
    size_t nread = 0;
    if (ok) {
        nread = fread(source, 1, sizeof(source) - 1, f);
        source[nread] = '\0';
        fclose(f);
    }
    if (ok) {
        ok = strstr(source, "strip_block_between_markers_inplace(") == NULL &&
             strstr(source, "strip_single_line_tag_prefix_inplace(") == NULL &&
             strstr(source, "strip_inline_transcript_suffix_inplace(") == NULL &&
             strstr(source, "tool_delegate_text_has_transcript_markup(") == NULL;
    }
    if (!ok) {
        pr_info("  delegate protocol sanitizer diag: %s", ok ? "" : source);
    }
    report("delegate protocol file hides markup sanitizer", ok);
}

static void test_delegate_protocol_file_hides_result_json_and_safe_output(void)
{
    char source[32768];
    FILE *f = fopen("/home/wangergou/code/github/daima-agent/drivers/tool/tool_delegate_protocol.c", "r");
    int ok = (f != NULL);
    size_t nread = 0;
    if (ok) {
        nread = fread(source, 1, sizeof(source) - 1, f);
        source[nread] = '\0';
        fclose(f);
    }
    if (ok) {
        ok = strstr(source, "bool tool_delegate_parse_result_json_summary(") == NULL &&
             strstr(source, "bool tool_delegate_parse_result_json_rendered(") == NULL &&
             strstr(source, "bool tool_delegate_extract_sync_final_output(") == NULL &&
             strstr(source, "void tool_delegate_build_safe_output_text(") == NULL &&
             strstr(source, "bool tool_delegate_try_fast_local_json(") == NULL;
    }
    if (!ok) {
        pr_info("  delegate protocol json/safe-output diag: %s", ok ? "" : source);
    }
    report("delegate protocol file hides result json and safe output", ok);
}

static void test_delegate_overview_file_hides_path_resolution_and_local_summary(void)
{
    char source[32768];
    FILE *f = fopen("/home/wangergou/code/github/daima-agent/drivers/tool/tool_delegate_overview.c", "r");
    int ok = (f != NULL);
    size_t nread = 0;
    if (ok) {
        nread = fread(source, 1, sizeof(source) - 1, f);
        source[nread] = '\0';
        fclose(f);
    }
    if (ok) {
        ok = strstr(source, "resolve_existing_path_with_fuzzy_components(") == NULL &&
             strstr(source, "collect_list_dir_paths(") == NULL &&
             strstr(source, "tool_delegate_try_local_repo_overview(") == NULL;
    }
    if (!ok) {
        pr_info("  delegate overview split diag: %s", ok ? "" : source);
    }
    report("delegate overview file hides path resolution and local summary", ok);
}

static void test_turn_gate_self_test_prompt_checks_workspace_and_log(void)
{
    char source[65536];
    FILE *f = fopen("/home/wangergou/code/github/daima-agent/kernel/turn/turn_gate.c", "r");
    int ok = (f != NULL);
    size_t nread = 0;

    if (ok) {
        nread = fread(source, 1, sizeof(source) - 1, f);
        source[nread] = '\0';
        fclose(f);
    }

    if (ok) {
        ok = strstr(source, "workspace_probe_prepare_opencode_repo(") != NULL &&
             strstr(source, "spiffs_data/workspace") != NULL &&
             strstr(source, "path_memory_dir()") != NULL &&
             strstr(source, "⚠️ 自检工作区准备失败") != NULL &&
             strstr(source, "自动 clone `https://github.com/sst/opencode.git` 也失败了") != NULL &&
             strstr(source, "有效仓库") != NULL &&
             strstr(source, "自检环境异常") != NULL &&
             strstr(source, "不要退回去分析整个 workspace") != NULL &&
             strstr(source, "delegate_store attach_task") != NULL &&
             strstr(source, "delegate_bg launch candidate") != NULL &&
             strstr(source, "delegate_bg restore queued child") != NULL &&
             strstr(source, "packages/app") != NULL &&
             strstr(source, "packages/cli") != NULL &&
             strstr(source, "packages/session-ui") != NULL &&
             strstr(source, "prepare_result.repo_present_before") != NULL &&
             strstr(source, "prepare_result.repo_ready_after") != NULL &&
             strstr(source, "const char *analysis_root = prepare_result.repo_path;") != NULL &&
             strstr(source, "if (!prepare_result.repo_ready_after) {") != NULL &&
             strstr(source, "不继续分析整个 workspace") != NULL &&
             strstr(source, "你现在只分析 `~/.agent-data/spiffs_data/workspace/opencode`") != NULL;
    }

    if (!ok) {
        pr_info("  turn gate self-test prompt diag: %s", ok ? "" : source);
    }
    report("turn gate self-test prompt checks workspace and runtime log", ok);
}

static void test_turn_gate_self_test_followup_prompt_targets_workspace_opencode_and_log(void)
{
    char prompt[4096];
    const char *analysis_root = "/home/wangergou/.agent-data/spiffs_data/workspace/opencode";
    const char *runtime_log_path = "/home/wangergou/.agent-data/spiffs_data/memory/agent.log";
    const char *log_marker = "self_test_marker:test_chat";
    bool ok;

    memset(prompt, 0, sizeof(prompt));
    ok = agent_turn_build_self_test_followup_prompt(prompt, sizeof(prompt),
                                                    analysis_root,
                                                    runtime_log_path,
                                                    log_marker);
    if (ok) {
        ok = strstr(prompt, "你现在只分析 `~/.agent-data/spiffs_data/workspace/opencode`") != NULL &&
             strstr(prompt, analysis_root) != NULL &&
             strstr(prompt, runtime_log_path) != NULL &&
             strstr(prompt, log_marker) != NULL &&
             strstr(prompt, "有效仓库") != NULL &&
             strstr(prompt, "自检环境异常") != NULL &&
             strstr(prompt, "不要退回去分析整个 workspace") != NULL &&
             strstr(prompt, "必须同时安排多个 subagent") != NULL &&
             strstr(prompt, "packages/app") != NULL &&
             strstr(prompt, "packages/cli") != NULL &&
             strstr(prompt, "packages/session-ui") != NULL &&
             strstr(prompt, "delegate_store attach_task") != NULL &&
             strstr(prompt, "delegate_bg launch candidate") != NULL &&
             strstr(prompt, "delegate_bg restore queued child") != NULL &&
             strstr(prompt, "只统计这个 marker 之后的日志") != NULL &&
             strstr(prompt, "2. 基于日志的多 subagent 运行结论。") != NULL;
    }

    if (!ok) {
        pr_info("  turn gate self-test followup prompt diag: %s", prompt);
    }
    report("turn gate self-test followup prompt targets workspace opencode and log", ok);
}

static void test_turn_gate_self_test_workspace_status_reports_repo_prepare_result(void)
{
    char msg_existing[1024];
    char msg_cloned[1024];
    const char *analysis_root = "/home/wangergou/.agent-data/spiffs_data/workspace/opencode";
    bool ok;

    memset(msg_existing, 0, sizeof(msg_existing));
    memset(msg_cloned, 0, sizeof(msg_cloned));
    ok = agent_turn_build_self_test_workspace_status(msg_existing,
                                                     sizeof(msg_existing),
                                                     analysis_root,
                                                     true,
                                                     true) &&
         agent_turn_build_self_test_workspace_status(msg_cloned,
                                                     sizeof(msg_cloned),
                                                     analysis_root,
                                                     false,
                                                     true);
    if (ok) {
        ok = strstr(msg_existing, "自检工作区预检") != NULL &&
             strstr(msg_existing, "直接复用") != NULL &&
             strstr(msg_existing, analysis_root) != NULL &&
             strstr(msg_existing, "多 subagent 分析") != NULL &&
             strstr(msg_cloned, "已自动 clone 一份 opencode 源码") != NULL &&
             strstr(msg_cloned, "agent.log") != NULL;
    }

    if (!ok) {
        pr_info("  turn gate self-test workspace status diag existing=%s cloned=%s",
                msg_existing,
                msg_cloned);
    }
    report("turn gate self-test workspace status reports repo prepare result", ok);
}

static void test_turn_gate_self_test_log_marker_uses_chat_id(void)
{
    char marker[256];
    bool ok;

    memset(marker, 0, sizeof(marker));
    ok = agent_turn_build_self_test_log_marker(marker, sizeof(marker), "chat_demo");
    if (ok) {
        ok = strstr(marker, "self_test_marker:chat_demo:") == marker;
    }
    if (!ok) {
        pr_info("  turn gate self-test log marker diag: %s", marker);
    }
    report("turn gate self-test log marker uses chat id", ok);
}

static void test_turn_gate_self_test_runtime_log_probe_counts_multi_subagents(void)
{
    char log_path[PATH_MAX];
    FILE *f = NULL;
    self_test_log_probe_t probe;
    bool ok = true;
    const char *marker = "self_test_marker:test_probe:1";

    snprintf(log_path, sizeof(log_path), "/tmp/daima-agent-self-test-log-probe.log");
    f = fopen(log_path, "w");
    if (!f) {
        report("turn gate self-test runtime log probe counts multi subagents", 0);
        return;
    }

    fputs("before marker should be ignored\n", f);
    fputs("delegate_store attach_task: coordinator=ignored\n", f);
    fprintf(f, "%s\n", marker);
    fputs("delegate_store attach_task: coordinator=dc_probe slot=0 task_id=dt_a\n", f);
    fputs("delegate_store attach_task: coordinator=dc_probe slot=1 task_id=dt_b\n", f);
    fputs("delegate_bg launch candidate: coordinator=dc_probe round=0 idx=0 task_id=dt_a\n", f);
    fputs("delegate_bg launch candidate: coordinator=dc_probe round=0 idx=1 task_id=dt_b\n", f);
    fputs("delegate_bg restore queued child: task_id=dt_b subagent=explore\n", f);
    fclose(f);

    memset(&probe, 0, sizeof(probe));
    ok = agent_turn_probe_self_test_runtime_log(log_path, marker, &probe);
    if (ok) {
        ok = probe.marker_found &&
             probe.attach_task_hits == 2 &&
             probe.launch_candidate_hits == 2 &&
             probe.restore_queued_hits == 1 &&
             probe.multi_subagent_confirmed;
    }
    unlink(log_path);
    if (!ok) {
        pr_info("  turn gate self-test log probe diag: marker=%d attach=%d launch=%d restore=%d confirmed=%d",
                probe.marker_found ? 1 : 0,
                probe.attach_task_hits,
                probe.launch_candidate_hits,
                probe.restore_queued_hits,
                probe.multi_subagent_confirmed ? 1 : 0);
    }
    report("turn gate self-test runtime log probe counts multi subagents", ok);
}

static void test_agent_self_test_results_json_includes_log_probe(void)
{
    char *json;
    bool ok;

    memset(&s_self_test_log_probe, 0, sizeof(s_self_test_log_probe));
    s_self_test_log_probe_pending = false;
    s_self_test_log_probe.marker_found = true;
    s_self_test_log_probe.attach_task_hits = 3;
    s_self_test_log_probe.launch_candidate_hits = 4;
    s_self_test_log_probe.restore_queued_hits = 2;
    s_self_test_log_probe.multi_subagent_confirmed = true;

    json = agent_self_test_results_json();
    ok = json != NULL &&
         strstr(json, "\"log_probe\":{") != NULL &&
         strstr(json, "\"marker_found\":true") != NULL &&
         strstr(json, "\"attach_task_hits\":3") != NULL &&
         strstr(json, "\"launch_candidate_hits\":4") != NULL &&
         strstr(json, "\"restore_queued_hits\":2") != NULL &&
         strstr(json, "\"pending\":false") != NULL &&
         strstr(json, "\"multi_subagent_confirmed\":true") != NULL;
    if (!ok) {
        pr_info("  self-test results json log probe diag: %s", json ? json : "<null>");
    }
    free(json);
    memset(&s_self_test_log_probe, 0, sizeof(s_self_test_log_probe));
    s_self_test_log_probe_pending = false;
    report("agent self-test results json includes log probe", ok);
}

static void test_turn_gate_self_test_defers_runtime_log_probe_until_followup(void)
{
    char source[65536];
    FILE *f = fopen("/home/wangergou/code/github/daima-agent/kernel/turn/turn_gate.c", "r");
    int ok = (f != NULL);
    size_t nread = 0;

    if (ok) {
        nread = fread(source, 1, sizeof(source) - 1, f);
        source[nread] = '\0';
        fclose(f);
    }

    if (ok) {
        ok = strstr(source, "agent_self_test_set_log_probe_pending(true);") != NULL &&
             strstr(source, "agent_turn_probe_self_test_runtime_log(runtime_log_path, log_marker,") == NULL;
    }

    if (!ok) {
        pr_info("  turn gate self-test runtime log deferral diag: %s", ok ? "" : source);
    }
    report("turn gate self-test defers runtime log probe until followup", ok);
}

static void test_agent_self_test_preflight_targets_workspace_opencode_only(void)
{
    FILE *f = fopen("/home/wangergou/code/github/daima-agent/kernel/self_test.c", "r");
    int ok = (f != NULL);
    char *source = NULL;
    long source_size = 0;
    size_t nread = 0;

    if (ok) {
        if (fseek(f, 0, SEEK_END) != 0) {
            ok = 0;
        } else {
            source_size = ftell(f);
            if (source_size <= 0 || fseek(f, 0, SEEK_SET) != 0) {
                ok = 0;
            }
        }
    }

    if (ok) {
        source = malloc((size_t)source_size + 1);
        ok = source != NULL;
    }

    if (ok) {
        nread = fread(source, 1, (size_t)source_size, f);
        source[nread] = '\0';
    }
    if (f) {
        fclose(f);
    }

    if (ok) {
        char *self_test_entry = strstr(source, "int agent_self_test(void)\n{");
        size_t entry_len = 0;

        if (self_test_entry) {
            int depth = 0;
            bool seen_open = false;
            char *p = self_test_entry;

            while (*p) {
                if (*p == '{') {
                    depth++;
                    seen_open = true;
                } else if (*p == '}') {
                    depth--;
                    if (seen_open && depth == 0) {
                        entry_len = (size_t)(p - self_test_entry + 1);
                        break;
                    }
                }
                p++;
            }
        }

        ok = self_test_entry != NULL &&
             entry_len > 0 &&
             strstr(self_test_entry, "workspace_probe_prepare_opencode_repo(") != NULL;
        if (ok) {
            const char *opencode_full_hit =
                strstr(self_test_entry, "workspace_probe_ensure_repo_clone(\"opencode_full\"");
            ok = !opencode_full_hit ||
                 (size_t)(opencode_full_hit - self_test_entry) >= entry_len;
        }
        if (ok) {
            ok = strstr(self_test_entry, "build_workspace_opencode_probe_path(") == NULL;
        }
    }

    if (!ok) {
        pr_info("  agent self-test preflight diag: %s", ok ? "" : source);
    }
    free(source);
    report("agent self-test preflight targets workspace opencode only", ok);
}

static void test_workspace_probe_prepare_opencode_repo_reports_ready_state(void)
{
    workspace_probe_repo_prepare_t prepare_result;
    bool ok;

    memset(&prepare_result, 0, sizeof(prepare_result));
    ok = workspace_probe_prepare_opencode_repo(&prepare_result);
    if (ok) {
        ok = prepare_result.repo_ready_after &&
             prepare_result.repo_path[0] != '\0' &&
             strstr(prepare_result.repo_path, "/workspace/opencode") != NULL;
    }
    report("workspace probe prepare opencode repo reports ready state", ok);
}

static void test_workspace_probe_repo_ready_detects_valid_opencode_clone(void)
{
    char repo_path[PATH_MAX];
    bool ok;

    memset(repo_path, 0, sizeof(repo_path));
    ok = workspace_probe_repo_ready("opencode", repo_path, sizeof(repo_path));
    if (ok) {
        char app_path[PATH_MAX];
        char cli_path[PATH_MAX];
        char session_ui_path[PATH_MAX];

        snprintf(app_path, sizeof(app_path), "%s/packages/app", repo_path);
        snprintf(cli_path, sizeof(cli_path), "%s/packages/cli", repo_path);
        snprintf(session_ui_path, sizeof(session_ui_path), "%s/packages/session-ui", repo_path);
        ok = access(app_path, F_OK) == 0 &&
             access(cli_path, F_OK) == 0 &&
             access(session_ui_path, F_OK) == 0;
    }
    report("workspace probe repo ready detects valid opencode clone", ok);
}

static void test_workspace_probe_ensure_repo_clone_populates_missing_path(void)
{
    char repo_path[PATH_MAX];
    char package_json_path[PATH_MAX];
    char packages_path[PATH_MAX];
    char expected_path[PATH_MAX];
    char probe_root[PATH_MAX];
    bool ok;

    build_self_test_probe_root(probe_root, sizeof(probe_root));
    build_workspace_opencode_probe_path(repo_path, sizeof(repo_path), NULL);
    snprintf(package_json_path, sizeof(package_json_path), "%s/package.json", repo_path);
    snprintf(packages_path, sizeof(packages_path), "%s/packages", repo_path);

    {
        char *mkdir_argv[] = {"mkdir", "-p", probe_root, NULL};
        char *rm_argv[] = {"rm", "-rf", repo_path, NULL};

        if (self_test_run_process_quiet("mkdir", mkdir_argv) != 0 ||
            self_test_run_process_quiet("rm", rm_argv) != 0) {
            report("workspace probe ensure clone populates missing path", 0);
            return;
        }
    }

    memset(repo_path, 0, sizeof(repo_path));
    build_workspace_opencode_probe_path(expected_path, sizeof(expected_path), NULL);
    ok = workspace_probe_ensure_repo_clone("opencode_probe",
                                           "https://github.com/sst/opencode.git",
                                           repo_path,
                                           sizeof(repo_path));
    if (ok) {
        ok = strcmp(repo_path, expected_path) == 0;
    }
    if (ok) {
        ok = access(package_json_path, F_OK) == 0 &&
             access(packages_path, F_OK) == 0;
    }
    report("workspace probe ensure clone populates missing path", ok);
}

static void test_workspace_probe_source_uses_remote_clone_without_local_seed(void)
{
    FILE *f = fopen("/home/wangergou/code/github/daima-agent/kernel/runtime/workspace_probe.c", "r");
    int ok = (f != NULL);
    char *source = NULL;
    long source_size = 0;
    size_t nread = 0;

    if (ok) {
        if (fseek(f, 0, SEEK_END) != 0) {
            ok = 0;
        } else {
            source_size = ftell(f);
            if (source_size <= 0 || fseek(f, 0, SEEK_SET) != 0) {
                ok = 0;
            }
        }
    }

    if (ok) {
        source = malloc((size_t)source_size + 1);
        ok = source != NULL;
    }

    if (ok) {
        nread = fread(source, 1, (size_t)source_size, f);
        source[nread] = '\0';
    }
    if (f) {
        fclose(f);
    }

    if (ok) {
        ok = strstr(source, "\"git\", \"clone\", \"--depth=1\"") != NULL &&
             strstr(source, "https://github.com/sst/opencode.git") != NULL &&
             strstr(source, "local seed clone") == NULL &&
             strstr(source, "try_clone_from_local_seed(") == NULL;
    }

    if (!ok) {
        pr_info("  workspace probe remote clone source diag: %s",
                source ? source : "<null>");
    }
    free(source);
    report("workspace probe source uses remote clone without local seed", ok);
}

static void test_workspace_probe_reclones_incomplete_repo_dir(void)
{
    char repo_path[PATH_MAX];
    char marker_path[PATH_MAX];
    char resolved_path[PATH_MAX];
    char probe_root[PATH_MAX];
    FILE *f = NULL;
    bool ok = true;

    build_self_test_probe_root(probe_root, sizeof(probe_root));
    build_workspace_opencode_probe_path(repo_path, sizeof(repo_path), NULL);
    snprintf(marker_path, sizeof(marker_path), "%s/STUB_INCOMPLETE_DIR", repo_path);

    {
        char *mkdir_probe_root_argv[] = {"mkdir", "-p", probe_root, NULL};
        char *rm_argv[] = {"rm", "-rf", repo_path, NULL};
        char *mkdir_argv[] = {"mkdir", "-p", repo_path, NULL};

        if (self_test_run_process_quiet("mkdir", mkdir_probe_root_argv) != 0 ||
            self_test_run_process_quiet("rm", rm_argv) != 0 ||
            self_test_run_process_quiet("mkdir", mkdir_argv) != 0) {
            ok = false;
        }
    }

    if (ok) {
        f = fopen(marker_path, "w");
        if (!f) {
            ok = false;
        } else {
            fputs("stub", f);
            fclose(f);
            f = NULL;
        }
    }

    if (!ok) {
        report("workspace probe reclones incomplete repo dir", 0);
        return;
    }

    if (ok) {
        memset(resolved_path, 0, sizeof(resolved_path));
        ok = workspace_probe_ensure_repo_clone("opencode_probe",
                                               "https://github.com/sst/opencode.git",
                                               resolved_path,
                                               sizeof(resolved_path));
    }
    if (ok) {
        ok = strcmp(resolved_path, repo_path) == 0;
    }
    if (ok) {
        ok = access(marker_path, F_OK) != 0;
    }
    if (ok) {
        f = fopen(resolved_path, "r");
        if (f) {
            fclose(f);
        }
        ok = access(marker_path, F_OK) != 0;
    }
    if (ok) {
        char packages_path[PATH_MAX];
        snprintf(packages_path, sizeof(packages_path), "%s/packages", resolved_path);
        ok = access(packages_path, F_OK) == 0;
    }
    report("workspace probe reclones incomplete repo dir", ok);
}

static void test_workspace_probe_reclones_layout_mismatch_repo_dir(void)
{
    char repo_path[PATH_MAX];
    char packages_path[PATH_MAX];
    char app_path[PATH_MAX];
    char cli_path[PATH_MAX];
    char package_json_path[PATH_MAX];
    char marker_path[PATH_MAX];
    char resolved_path[PATH_MAX];
    char probe_root[PATH_MAX];
    FILE *f = NULL;
    bool ok = true;

    build_self_test_probe_root(probe_root, sizeof(probe_root));
    build_workspace_opencode_probe_path(repo_path, sizeof(repo_path), NULL);
    snprintf(packages_path, sizeof(packages_path), "%s/packages", repo_path);
    snprintf(app_path, sizeof(app_path), "%s/packages/app", repo_path);
    snprintf(cli_path, sizeof(cli_path), "%s/packages/cli", repo_path);
    snprintf(package_json_path, sizeof(package_json_path), "%s/package.json", repo_path);
    snprintf(marker_path, sizeof(marker_path), "%s/LAYOUT_MISMATCH_STUB", repo_path);

    {
        char *mkdir_probe_root_argv[] = {"mkdir", "-p", probe_root, NULL};
        char *rm_argv[] = {"rm", "-rf", repo_path, NULL};
        char *mkdir_repo_argv[] = {"mkdir", "-p", repo_path, NULL};
        char *mkdir_packages_argv[] = {"mkdir", "-p", packages_path, NULL};
        char *mkdir_app_argv[] = {"mkdir", "-p", app_path, NULL};
        char *mkdir_cli_argv[] = {"mkdir", "-p", cli_path, NULL};

        if (self_test_run_process_quiet("mkdir", mkdir_probe_root_argv) != 0 ||
            self_test_run_process_quiet("rm", rm_argv) != 0 ||
            self_test_run_process_quiet("mkdir", mkdir_repo_argv) != 0 ||
            self_test_run_process_quiet("mkdir", mkdir_packages_argv) != 0 ||
            self_test_run_process_quiet("mkdir", mkdir_app_argv) != 0 ||
            self_test_run_process_quiet("mkdir", mkdir_cli_argv) != 0) {
            ok = false;
        }
    }

    if (ok) {
        f = fopen(package_json_path, "w");
        if (!f) {
            ok = false;
        } else {
            fputs("{\"name\":\"stub-opencode\"}\n", f);
            fclose(f);
            f = NULL;
        }
    }

    if (ok) {
        f = fopen(marker_path, "w");
        if (!f) {
            ok = false;
        } else {
            fputs("stub", f);
            fclose(f);
            f = NULL;
        }
    }

    if (!ok) {
        report("workspace probe reclones layout mismatch repo dir", 0);
        return;
    }

    memset(resolved_path, 0, sizeof(resolved_path));
    ok = workspace_probe_ensure_repo_clone("opencode_probe",
                                           "https://github.com/sst/opencode.git",
                                           resolved_path,
                                           sizeof(resolved_path));
    if (ok) {
        ok = strcmp(resolved_path, repo_path) == 0;
    }
    if (ok) {
        ok = access(marker_path, F_OK) != 0;
    }
    if (ok) {
        char session_ui_path[PATH_MAX];
        snprintf(session_ui_path, sizeof(session_ui_path), "%s/packages/session-ui",
                 resolved_path);
        ok = access(session_ui_path, F_OK) == 0;
    }
    report("workspace probe reclones layout mismatch repo dir", ok);
}

static void test_workspace_probe_prepare_opencode_repo_cleans_legacy_full_clone(void)
{
    char legacy_path[PATH_MAX];
    char marker_path[PATH_MAX];
    FILE *f = NULL;
    workspace_probe_repo_prepare_t prepare_result;
    bool ok = true;

    snprintf(legacy_path, sizeof(legacy_path), "%s/opencode_full", path_workspace_dir());
    snprintf(marker_path, sizeof(marker_path), "%s/LEGACY_SELF_TEST_STUB", legacy_path);

    {
        char *mkdir_argv[] = {"mkdir", "-p", legacy_path, NULL};
        char *rm_argv[] = {"rm", "-rf", legacy_path, NULL};

        if (self_test_run_process_quiet("rm", rm_argv) != 0 ||
            self_test_run_process_quiet("mkdir", mkdir_argv) != 0) {
            ok = false;
        }
    }

    if (ok) {
        f = fopen(marker_path, "w");
        if (!f) {
            ok = false;
        } else {
            fputs("legacy", f);
            fclose(f);
            f = NULL;
        }
    }

    if (ok) {
        memset(&prepare_result, 0, sizeof(prepare_result));
        ok = workspace_probe_prepare_opencode_repo(&prepare_result);
    }
    if (ok) {
        ok = prepare_result.repo_ready_after &&
             strstr(prepare_result.repo_path, "/workspace/opencode") != NULL;
    }
    if (ok) {
        ok = access(legacy_path, F_OK) != 0;
    }

    report("workspace probe prepare opencode repo cleans legacy full clone", ok);
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
    long original_size = size;
    if (size > 131072) { read_from = size - 131072; size = 131072; }
    fseek(f, read_from, SEEK_SET);

    char *buf = kmalloc(size + 1, GFP_KERNEL);
    if (!buf) { fclose(f); report("log self-check (OOM)", 0); return; }
    fread(buf, 1, size, f);
    fclose(f);
    buf[size] = '\0';

    /* 检查健康指标 */
    int ok = 1;

    /* 关键子系统初始化。
     * 日志只扫描尾部 128 KiB；对大日志不能要求必须包含早期启动行。
     * --self-test fast path 也不会进入完整主循环，因此初始化信号只做
     * “尾部可见时校验”，不可见时退化为软提示。 */
    bool saw_bus_ready = strstr(buf, "Bus subsystem ready") != NULL;
    bool saw_loop_ready = strstr(buf, "Agent loop started") != NULL ||
                          strstr(buf, "Agent loop initialized") != NULL;
    bool saw_self_test_banner = strstr(buf, "Agent Self-Test") != NULL;
    bool tail_truncated = read_from > 0 || original_size >= 131072;

    if (!saw_bus_ready && !tail_truncated && !saw_self_test_banner)
        { pr_warn("  missing: Bus subsystem ready"); ok = 0; }
    if (!saw_loop_ready && !tail_truncated && !saw_self_test_banner)
        { pr_warn("  missing: Agent loop startup/initialization"); ok = 0; }

    /* 工具执行记录：
     * - "Executor: tool_name → err=0"
     * - "Tool xxx result:"
     * - "execute result tool=... err=0"
     */
    int tool_ok = 0;
    const char *p = buf;
    while ((p = strstr(p, "Executor:")) != NULL) {
        if (strstr(p, "err=0")) {
            tool_ok++;
        }
        p += strlen("Executor:");
    }
    p = buf;
    while ((p = strstr(p, "Tool ")) != NULL) {
        if (strstr(p, " result:")) {
            tool_ok++;
        }
        p += strlen("Tool ");
    }
    p = buf;
    while ((p = strstr(p, "execute result tool=")) != NULL) {
        if (strstr(p, " err=0")) {
            tool_ok++;
        }
        p += strlen("execute result tool=");
    }
    if (tool_ok == 0 && !tail_truncated && saw_self_test_banner) {
        pr_warn("  no tool execution records");
        ok = 0;
    }

    /* 无崩溃信号 */
    if (strstr(buf, "SIGSEGV") || strstr(buf, "stack overflow"))
        { pr_warn("  crash indicator in log"); ok = 0; }

    pr_info("  log: %ld bytes, %d tool records (err=0), init OK=%s tail_truncated=%s self_test_seen=%s",
            size, tool_ok,
            (saw_bus_ready || saw_loop_ready) ? "yes" : "no",
            tail_truncated ? "yes" : "no",
            saw_self_test_banner ? "yes" : "no");

    kfree(buf);
    report("log self-check (health scan)", ok);
}

int agent_self_test(void)
{
    pr_info("========================================");
    pr_info("  Agent Self-Test — Multi-Core Check");
    pr_info("========================================");

    {
        workspace_probe_repo_prepare_t prepare_result;
        memset(&prepare_result, 0, sizeof(prepare_result));
        if (!workspace_probe_prepare_opencode_repo(&prepare_result)) {
            pr_warn("self-test preflight: failed to ensure opencode workspace at %s",
                    prepare_result.repo_path[0] ? prepare_result.repo_path : "<unresolved>");
        }
    }

    /* 等各核启动 */
    usleep(500000);

    test_executor_queue();
    test_intent_action_request_heuristic();
    test_intent_fallback_keeps_action_requests_as_implement();
    test_message_bus();
    test_tool_bus_bindings();
    test_memory_queue();
    test_host_portability_runtime();
    test_real_tool_via_executor();
    test_message_pipeline();
    test_async_compress_dispatch();
    test_delegate_task_legacy_rejected();
    test_delegate_task_store_done_update_after_parent_response();
    test_delegate_task_store_staged_counts();
    test_delegate_task_store_requires_effective_output_for_done();
    test_delegate_task_store_plan_fails_when_store_is_full();
    test_delegate_task_store_retains_child_session_history();
    test_delegate_task_store_plan_retains_preflight_tool();
    test_delegate_task_store_records_child_session_step_event();
    test_delegate_task_store_step_event_renders_result_json_visible_text();
    test_delegate_runtime_tool_call_records_child_session_step();
    test_delegate_task_store_step_history_precedes_done();
    test_delegate_task_store_records_child_session_message_commits();
    test_delegate_task_store_retains_richer_child_session_history_window();
    test_delegate_task_store_session_seq_stays_monotonic_across_trim();
    test_delegate_child_session_json_retains_recent_history_window();
    test_delegate_child_session_json_exposes_normalized_session_fields();
    test_delegate_child_session_json_exposes_window_metadata();
    test_delegate_child_session_json_history_exposes_timestamps();
    test_delegate_child_session_json_history_exposes_sources();
    test_delegate_child_session_json_history_exposes_ids();
    test_delegate_child_session_json_history_exposes_seq();
    test_delegate_child_session_json_history_seq_stays_monotonic_across_window();
    test_delegate_child_session_json_filters_incremental_after_seq();
    test_delegate_child_session_json_marks_replay_reset_when_after_seq_falls_outside_window();
    test_delegate_child_session_json_frames_and_commits_expose_ids();
    test_delegate_child_session_json_frames_and_commits_expose_seq();
    test_delegate_child_session_json_renders_result_json_visible_text();
    test_delegate_child_session_preferred_visible_text_prefers_latest_frame_over_stale_history();
    test_delegate_turn_session_persists_full_child_transcript();
    test_delegate_child_session_json_does_not_leak_reused_session_history();
    test_sudo_request_routes_delegate_child_to_parent_context();
    test_interactive_request_routes_delegate_child_to_parent_context();
    test_delegate_task_store_persists_pending_interactive_request();
    test_turn_context_persists_parent_pending_interactive_request();
    test_delegate_parent_wake_waits_for_parent_response();
    test_delegate_parent_wake_sends_done_after_completion();
    test_delegate_parent_wake_subagent_event_exposes_visible_output();
    test_delegate_parent_wake_does_not_complete_empty_output();
    test_delegate_parent_wake_retries_failed_done_send();
    test_delegate_parent_wake_retries_missing_parent_client();
    test_delegate_parent_wake_terminal_missing_parent_client_completes();
    test_delegate_parent_wake_defers_while_parent_recently_active();
    test_delegate_parent_wake_retains_terminal_resume_after_visible_dispatch();
    test_delegate_parent_wake_drops_retained_resume_after_parent_activity();
    test_delegate_parent_wake_recent_activity_alone_does_not_drop_retained_resume();
    test_delegate_parent_wake_drops_retained_resume_after_parent_assistant_output();
    test_delegate_parent_wake_retains_failed_resume_after_parent_activity();
    test_delegate_parent_wake_defers_resume_while_parent_has_pending_request();
    test_delegate_parent_wake_does_not_repeat_same_visible_revision();
    test_delegate_parent_wake_ignores_unchanged_coordinators();
    test_delegate_parent_subagent_state_json_uses_shared_projection();
    test_delegate_session_state_json_unifies_parent_history_and_subagents();
    test_delegate_subagent_session_delta_json_uses_incremental_projection();
    test_delegate_subagent_session_deltas_json_batches_incremental_projection();
    test_delegate_parent_subagent_state_delta_json_batches_visible_revision_and_sessions();
    test_delegate_parent_subagent_state_delta_json_includes_changed_coordinator_sessions_without_explicit_tasks();
    test_delegate_parent_registry_exposes_wake_lifecycle();
    test_delegate_stored_directive_shortcut_starts_background_delegate();
    test_delegate_background_launch_respects_running_budget();
    test_delegate_background_launch_keeps_capacity_for_real_batch();
    test_delegate_lifecycle_runtime_launches_across_coordinators_fairly();
    test_delegate_lifecycle_runtime_respects_per_coordinator_cap();
    test_delegate_lifecycle_runtime_respects_per_parent_cap();
    test_delegate_lifecycle_runtime_ignores_blocked_for_coordinator_cap();
    test_delegate_lifecycle_runtime_ignores_blocked_for_parent_cap();
    test_delegate_background_launch_does_not_finish_with_queued_children();
    test_delegate_background_launch_long_prompts_keep_all_children_schedulable();
    test_delegate_task_sync_implement();
    test_delegate_task_background_handle();
    test_delegate_task_batch_background_returns_coordinator();
    test_delegate_task_batch_poll_returns_agents();
    test_delegate_batch_background_returns_all_session_ids();
    test_delegate_batch_explicit_scope_children_prefer_local_overview();
    test_delegate_batch_explore_children_with_path_are_normalized();
    test_delegate_background_local_overview_shortcut_records_child_session_step();
    test_delegate_background_dependency_merge_shortcut_records_child_session_step();
    test_delegate_task_parent_registry_list();
    test_delegate_completion_turn_hides_delegate_tool();
    test_tool_guard_detects_non_advertised_tool();
    test_delegate_completion_turn_uses_no_tools();
    test_delegate_completion_turn_merges_locally();
    test_delegate_completion_turn_prefers_child_session_rendered_summary();
    test_delegate_background_coordinator_summary_prefers_child_session_rendered_text();
    test_delegate_state_json_agent_summary_prefers_child_session_rendered_text();
    test_delegate_completion_turn_summarizes_cross_module_relationships();
    test_delegate_completion_turn_summarizes_explicit_scope_boundaries();
    test_delegate_completion_outbound_does_not_rearm_parent_wake();
    test_model_fallback_primary_override_uses_matching_provider();
    test_context_prompt_mentions_batch_delegate();
    test_turn_exec_merges_sibling_delegate_calls();
    test_turn_exec_rewrites_root_list_into_explicit_multi_scope_batch();
    test_turn_exec_rewrites_explicit_relative_scope_list_into_batch();
    test_turn_exec_marks_background_delegate_started();
    test_turn_exec_merges_same_turn_broad_discovery_after_background_start();
    test_delegate_empty_input_is_recoverable();
    test_delegate_empty_input_is_recoverable_noise();
    test_delegate_schema_avoids_anyof();
    test_turn_gate_self_test_prompt_checks_workspace_and_log();
    test_turn_gate_self_test_followup_prompt_targets_workspace_opencode_and_log();
    test_turn_gate_self_test_workspace_status_reports_repo_prepare_result();
    test_turn_gate_self_test_log_marker_uses_chat_id();
    test_turn_gate_self_test_runtime_log_probe_counts_multi_subagents();
    test_turn_gate_self_test_defers_runtime_log_probe_until_followup();
    test_agent_self_test_results_json_includes_log_probe();
    test_agent_self_test_preflight_targets_workspace_opencode_only();
    test_workspace_probe_prepare_opencode_repo_reports_ready_state();
    test_workspace_probe_prepare_opencode_repo_cleans_legacy_full_clone();
    test_workspace_probe_repo_ready_detects_valid_opencode_clone();
    test_workspace_probe_ensure_repo_clone_populates_missing_path();
    test_workspace_probe_source_uses_remote_clone_without_local_seed();
    test_workspace_probe_reclones_incomplete_repo_dir();
    test_workspace_probe_reclones_layout_mismatch_repo_dir();
    test_log_self_check();
    test_discovery_files_rewritten_to_delegate();
    test_discovery_terminal_rewritten_to_delegate();
    test_interview_structured_batch_preserved_in_files_rewrite();
    test_interview_structured_batch_preserved_in_terminal_rewrite();
    test_interview_structured_batch_overrides_direct_delegate_task();
    test_interview_structured_batch_overrides_existing_delegate_batch();
    test_discovery_files_rewritten_without_investigate_intent();
    test_subagent_discovery_not_rewritten_recursively();
    test_subagent_tool_activity_does_not_require_ws_client();
    test_websocket_tool_activity_disconnect_is_best_effort();
    test_discovery_prompt_marked_as_bounded();
    test_delegate_explore_overview_budget_heuristic();
    test_delegate_local_repo_overview_prefers_structured_target_path();
    test_delegate_local_repo_overview_does_not_refine_structured_repo_root_from_background();
    test_delegate_local_repo_overview_keeps_repo_root_for_top_level_request();
    test_delegate_repo_root_overview_prefers_batch_expansion();
    test_delegate_task_single_repo_root_explore_expands_to_batch();
    test_delegate_task_runtime_style_repo_root_explore_expands_to_batch();
    test_delegate_task_logged_runtime_repo_root_explore_expands_to_batch();
    test_delegate_task_oh_my_openagent_explicit_scopes_expand_to_batch();
    test_delegate_task_opencode_explicit_scopes_expand_to_batch();
    test_delegate_repo_root_explicit_scopes_override_deep_analysis_gate();
    test_delegate_request_accepts_preflight_tool();
    test_delegate_batch_accepts_child_preflight_tool();
    test_delegate_task_repo_root_target_path_explore_expands_to_batch();
    test_discovery_patch_skips_delegate_subagent_chat();
    test_discovery_patch_skips_internal_broad_discovery_message();
    test_discovery_patch_skips_explicit_multi_subagent_request();
    test_delegate_dsml_output_filter();
    test_delegate_safe_output_returns_protocol_failure_summary();
    test_delegate_safe_output_rejects_transcript_markup();
    test_delegate_safe_output_includes_excerpt_for_non_json_text();
    test_delegate_safe_output_prefers_reasoning_on_budget_exhausted();
    test_delegate_safe_output_prefers_json_summary_on_budget_exhausted();
    test_delegate_fast_local_json_accepts_valid_json();
    test_delegate_fast_local_json_wraps_safe_text();
    test_delegate_fast_local_json_normalizes_terminal_result();
    test_delegate_fast_local_json_rejects_files_tool_json();
    test_delegate_local_repo_overview_shortcut_summarizes_kernel_dir();
    test_delegate_local_repo_overview_shortcut_summarizes_explicit_subdir();
    test_delegate_local_repo_overview_accepts_target_path_focused_scope();
    test_delegate_local_repo_overview_filters_build_artifacts();
    test_delegate_dependency_merge_shortcut_uses_conclusion_style();
    test_delegate_dependency_merge_shortcut_renders_valid_result_json();
    test_delegate_dependency_merge_shortcut_backfills_evidence_without_upstream_section();
    test_delegate_local_repo_overview_accepts_backtick_wrapped_path();
    test_delegate_local_repo_overview_accepts_directory_tree_prompt();
    test_websocket_client_chat_id_roundtrip_accepts_32_char_id();
    test_delegate_explore_prompt_mentions_real_files_search_protocol();
    test_delegate_result_json_parser_accepts_valid_json();
    test_delegate_result_json_renderer_accepts_blocked_json();
    test_delegate_all_subagents_prefer_structured_output();
    test_delegate_extract_sync_final_output_accepts_done_wrapper();
    test_delegate_extract_sync_final_output_rejects_blocked_wrapper();
    test_turn_exec_marks_sync_delegate_completed();
    test_delegate_tool_description_mentions_no_duplicate_and_batch();
    test_delegate_prepare_single_file_prompt_injects_context();
    test_delegate_prepare_single_file_prompt_truncates_context();
    test_delegate_prepare_overview_prompt_is_bounded();
    test_delegate_batch_child_prompt_injects_target_path_contract();
    test_delegate_explore_prompt_forbids_fake_repo_roots();
    test_turn_interview_appends_answer_into_current_message();
    test_turn_interview_answer_requests_continue_turn();
    test_turn_interview_answer_stores_delegate_directive();
    test_ws_interactive_reply_stores_delegate_directive();
    test_prometheus_interview_specificity_gate();
    test_prometheus_force_interview_for_repo_wide_vague_implement();
    test_delegate_task_id_parse_without_subagent_type();
    test_delegate_task_id_poll_returns_json_wrapper();
    test_delegate_public_header_hides_internal_helpers();
    test_delegate_sync_file_hides_preflight_runner();
    test_delegate_sync_file_hides_result_finalizer();
    test_delegate_runtime_file_hides_background_worker_launch();
    test_delegate_runtime_file_hides_batch_prepare_and_restore_scan();
    test_delegate_runtime_file_hides_lifecycle_entry();
    test_delegate_lifecycle_file_owns_runtime_entry();
    test_agent_loop_file_hides_delegate_lifecycle_steps();
    test_delegate_lifecycle_file_owns_poll_entry();
    test_delegate_lifecycle_file_owns_runtime_candidate_filter();
    test_delegate_lifecycle_runtime_entry_hides_inline_candidate_filter();
    test_delegate_parent_wake_file_owns_terminal_resume_gate();
    test_delegate_parent_wake_flush_hides_inline_terminal_resume_gate();
    test_delegate_parent_wake_file_uses_consumed_resume_gate();
    test_delegate_parent_wake_file_owns_resume_pending_derivation();
    test_delegate_parent_wake_hides_inline_resume_pending_derivation();
    test_delegate_parent_wake_file_owns_resume_state_helpers();
    test_delegate_parent_wake_flush_hides_inline_resume_state_updates();
    test_delegate_parent_wake_file_owns_dispatch_helpers();
    test_delegate_parent_wake_flush_hides_inline_sender_calls();
    test_delegate_parent_wake_file_owns_visible_update_helper();
    test_delegate_parent_wake_flush_hides_inline_visible_update_sequence();
    test_delegate_protocol_file_hides_markup_sanitizer();
    test_delegate_protocol_file_hides_result_json_and_safe_output();
    test_delegate_overview_file_hides_path_resolution_and_local_summary();
    wait_for_delegate_background_idle_for_test();

    pr_info("----------------------------------------");
    pr_info("  Results: %d/%d passed", tests_pass, tests_run);
    pr_info("========================================");
    return tests_pass == tests_run ? 0 : 1;
}
