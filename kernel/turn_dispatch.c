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
err_t dispatch_save_session(const char *chat_id, const char *role, const char *content)
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
    task.payload = cJSON_PrintUnformatted(payload);
    cJSON_Delete(payload);
    task.timeout_ms = 0;  /* 无需回复 */

    pr_debug("dispatch: save_session → memory_core (task %s)", task.id);
    err_t ret = core_send(CORE_MEMORY, &task);
    kfree(task.payload);
    return ret;
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
    kfree(task.payload);
    return ret;
}