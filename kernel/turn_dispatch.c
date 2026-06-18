/* 大核调度层：将原有同步调用转为异步 core_task */
#include "linux/core_task.h"
#include "linux/printk.h"
#include "linux/slab.h"
#include "linux/kernel.h"
#include "cjson.h"
#include <stdio.h>
#include <time.h>

static int s_task_seq = 0;

/* 向执行核发工具执行任务（fire-and-forget） */
err_t dispatch_execute_tools(const char *tools_json)
{
    struct core_task task;
    memset(&task, 0, sizeof(task));
    snprintf(task.id, sizeof(task.id), "et_%d", ++s_task_seq);
    strscpy(task.type, TASK_EXECUTE_TOOLS, sizeof(task.type));
    strscpy(task.status, TASK_PENDING, sizeof(task.status));
    task.payload = strdup(tools_json ? tools_json : "{}");
    task.timeout_ms = 30000;

    pr_debug("dispatch: execute_tools → executor_core (task %s)", task.id);
    return core_send(CORE_EXECUTOR, &task);
}

/* 向记忆核发会话保存任务（fire-and-forget，不等待回复） */
static err_t dispatch_save_session_ex(const char *chat_id, const char *role,
                                       const char *content, const char *source)
{
    struct core_task task;
    memset(&task, 0, sizeof(task));
    snprintf(task.id, sizeof(task.id), "ss_%d", ++s_task_seq);
    strscpy(task.type, TASK_SAVE_SESSION, sizeof(task.type));
    strscpy(task.status, TASK_PENDING, sizeof(task.status));

    cJSON *payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "chat_id", chat_id);
    cJSON_AddStringToObject(payload, "role", role);
    cJSON_AddStringToObject(payload, "content", content);
    if (source && source[0])
        cJSON_AddStringToObject(payload, "source", source);
    task.payload = cJSON_PrintUnformatted(payload);
    cJSON_Delete(payload);
    task.timeout_ms = 0;  /* 无需回复 */

    pr_debug("dispatch: save_session → memory_core (task %s)", task.id);
    return core_send(CORE_MEMORY, &task);
}

err_t dispatch_save_session(const char *chat_id, const char *role, const char *content)
{
    return dispatch_save_session_ex(chat_id, role, content, NULL);
}

err_t dispatch_save_session_sourced(const char *chat_id, const char *role,
                                     const char *content, const char *source)
{
    return dispatch_save_session_ex(chat_id, role, content, source);
}

/* 向记忆核发上下文压缩任务（fire-and-forget） */
err_t dispatch_compress_context(const char *chat_id)
{
    struct core_task task;
    memset(&task, 0, sizeof(task));
    snprintf(task.id, sizeof(task.id), "cc_%d", ++s_task_seq);
    strscpy(task.type, TASK_COMPRESS_CONTEXT, sizeof(task.type));
    strscpy(task.status, TASK_PENDING, sizeof(task.status));

    cJSON *payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "chat_id", chat_id);
    task.payload = cJSON_PrintUnformatted(payload);
    cJSON_Delete(payload);
    task.timeout_ms = 0;

    pr_debug("dispatch: compress_context → memory_core (task %s)", task.id);
    err_t ret = core_send(CORE_MEMORY, &task);
    return ret;
}

/* 同步桥接：向执行核发工具执行任务，阻塞等回复 */
err_t tool_execute_via_core(const char *name, const char *input,
                             char *output, size_t output_size)
{
    struct core_task task;
    memset(&task, 0, sizeof(task));
    snprintf(task.id, sizeof(task.id), "et_%d", ++s_task_seq);
    strscpy(task.type, TASK_EXECUTE_TOOLS, sizeof(task.type));
    task.timeout_ms = 30000;

    cJSON *payload = cJSON_CreateObject();
    cJSON *tools = cJSON_CreateArray();
    cJSON *tool = cJSON_CreateObject();
    cJSON_AddStringToObject(tool, "id", "1");
    cJSON_AddStringToObject(tool, "name", name);
    cJSON_AddStringToObject(tool, "input", input);
    cJSON_AddItemToArray(tools, tool);
    cJSON_AddItemToObject(payload, "tools", tools);
    task.payload = cJSON_PrintUnformatted(payload);
    cJSON_Delete(payload);

    if (core_send(CORE_EXECUTOR, &task) != 0) {
        pr_err("dispatch: core_send to executor_core failed for task %s", task.id);
        kfree(task.payload);
        return ERR_FAIL;
    }

    /* 阻塞等回复，按 task_id 匹配过滤，避免消费其他核的回复 */
    struct core_task reply;
    err_t err = ERR_TIMEOUT;
    for (int retry = 0; retry < 100; retry++) {
        memset(&reply, 0, sizeof(reply));
        err = core_recv(CORE_SCHEDULER, &reply, 3000);
        if (err != 0) {
            snprintf(output, output_size, "executor core timeout");
            return ERR_TIMEOUT;
        }
        if (strcmp(reply.id, task.id) == 0)
            break;
        /* 不是我们的回复：放回队列（fire-and-forget 回复由 process_core_reply 处理） */
        core_reply(&reply);
    }
    if (err != 0 || strcmp(reply.id, task.id) != 0) {
        snprintf(output, output_size, "executor core timeout");
        return ERR_TIMEOUT;
    }

    cJSON *root = cJSON_Parse(reply.result ? reply.result : "{}");
    cJSON *results = cJSON_GetObjectItem(root, "results");
    if (results && cJSON_IsArray(results)) {
        cJSON *r = cJSON_GetArrayItem(results, 0);
        if (r) {
            const char *out = cJSON_GetStringValue(cJSON_GetObjectItem(r, "output"));
            cJSON *err_j = cJSON_GetObjectItem(r, "err");
            if (out) strscpy(output, out, output_size);
            err = err_j ? (err_t)cJSON_GetNumberValue(err_j) : ERR_FAIL;
        }
    }
    cJSON_Delete(root);
    kfree(reply.result);
    return err;
}