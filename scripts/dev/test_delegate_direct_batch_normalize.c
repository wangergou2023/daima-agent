#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cjson.h"
#include "drivers/tool/tool_invocation_context.h"
#include "drivers/llm/llm_proxy.h"
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

static cJSON *find_task_by_key(cJSON *tasks, const char *task_key)
{
    cJSON *item = NULL;

    if (!tasks || !cJSON_IsArray(tasks) || !task_key || !task_key[0]) {
        return NULL;
    }
    cJSON_ArrayForEach(item, tasks) {
        const char *key = cJSON_GetStringValue(cJSON_GetObjectItem(item, "task_key"));
        if (key && strcmp(key, task_key) == 0) {
            return item;
        }
    }
    return NULL;
}

int main(void)
{
    struct message msg = {0};
    llm_tool_call_t call = {0};
    char *patched = NULL;
    cJSON *root = NULL;
    cJSON *tasks = NULL;
    cJSON *oracle = NULL;
    cJSON *depends = NULL;
    const char *dispatch_mode = NULL;

    strscpy(msg.channel, CHAN_WEBSOCKET, sizeof(msg.channel));
    strscpy(msg.chat_id, "probe_direct_batch", sizeof(msg.chat_id));
    strscpy(msg.source, MSG_SOURCE_USER, sizeof(msg.source));
    msg.intent = INTENT_INVESTIGATE;
    msg.content = strdup("请分析 /home/wangergou/code/github/codex 的代码框架，分别说明 CLI 入口、核心执行层、工具层、文档层职责，并给出建议阅读顺序。");
    if (!msg.content) {
        return fail("unable to allocate message content");
    }

    strscpy(call.name, "delegate_task", sizeof(call.name));
    call.input = strdup(
        "{"
        "\"dispatch_mode\":\"parallel\","
        "\"tasks\":["
        "{\"task_key\":\"cli_entry\",\"description\":\"分析 CLI 入口\",\"subagent_type\":\"explore\",\"target_path\":\"/home/wangergou/code/github/codex\",\"prompt\":\"分析 CLI 入口与启动链路\"},"
        "{\"task_key\":\"core_exec\",\"description\":\"分析核心执行层\",\"subagent_type\":\"explore\",\"target_path\":\"/home/wangergou/code/github/codex\",\"prompt\":\"分析核心执行层与主流程\"},"
        "{\"task_key\":\"tools_layer\",\"description\":\"分析工具层\",\"subagent_type\":\"explore\",\"target_path\":\"/home/wangergou/code/github/codex\",\"prompt\":\"分析工具层职责与扩展点\"},"
        "{\"task_key\":\"docs_layer\",\"description\":\"分析文档层\",\"subagent_type\":\"explore\",\"target_path\":\"/home/wangergou/code/github/codex\",\"prompt\":\"分析 docs 与开发文档职责\"}"
        "]"
        "}");
    if (!call.input) {
        free(msg.content);
        return fail("unable to allocate tool input");
    }
    call.input_len = strlen(call.input);

    patched = tool_invocation_context_patch_input(&call, &msg);
    if (!patched) {
        free(call.input);
        free(msg.content);
        return fail("expected delegate_task batch to be normalized");
    }

    root = cJSON_Parse(patched);
    if (!root || !cJSON_IsObject(root)) {
        free(patched);
        free(call.input);
        free(msg.content);
        cJSON_Delete(root);
        return fail("patched payload is not valid json");
    }

    dispatch_mode = cJSON_GetStringValue(cJSON_GetObjectItem(root, "dispatch_mode"));
    if (!dispatch_mode || strcmp(dispatch_mode, "staged") != 0) {
        free(patched);
        free(call.input);
        free(msg.content);
        cJSON_Delete(root);
        return fail("patched payload must switch to staged dispatch");
    }

    tasks = cJSON_GetObjectItem(root, "tasks");
    if (!tasks || !cJSON_IsArray(tasks) || cJSON_GetArraySize(tasks) < 5) {
        free(patched);
        free(call.input);
        free(msg.content);
        cJSON_Delete(root);
        return fail("patched payload must include oracle task");
    }

    oracle = find_task_by_key(tasks, "oracle_synthesis");
    if (!oracle) {
        free(patched);
        free(call.input);
        free(msg.content);
        cJSON_Delete(root);
        return fail("oracle_synthesis task missing");
    }
    if (strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(oracle, "subagent_type")), "oracle") != 0) {
        free(patched);
        free(call.input);
        free(msg.content);
        cJSON_Delete(root);
        return fail("oracle task has wrong subagent_type");
    }

    depends = cJSON_GetObjectItem(oracle, "depends_on");
    if (!depends || !cJSON_IsArray(depends) || cJSON_GetArraySize(depends) < 4) {
        free(patched);
        free(call.input);
        free(msg.content);
        cJSON_Delete(root);
        return fail("oracle task missing dependencies");
    }

    cJSON_Delete(root);
    free(patched);
    free(call.input);
    free(msg.content);
    puts("PASS");
    return 0;
}
