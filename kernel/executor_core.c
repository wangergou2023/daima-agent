/* 执行核：无状态工具执行循环 */
#include "linux/core_task.h"
#include "linux/printk.h"
#include "linux/slab.h"
#include "drivers/tool/tool_registry.h"
#include "cjson.h"
#include "os.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

#define TOOL_OUTPUT_SIZE 65536

static void executor_task(void *arg)
{
    (void)arg;
    pr_info("Executor core started");

    char *output = kmalloc(TOOL_OUTPUT_SIZE, GFP_KERNEL);
    if (!output) { pr_err("Executor: no memory for output buffer"); return; }

    while (1) {
        struct core_task task;
        memset(&task, 0, sizeof(task));

        if (core_recv(CORE_EXECUTOR, &task, UINT32_MAX) != 0) continue;
        if (strcmp(task.type, TASK_EXECUTE_TOOLS) != 0) {
            kfree(task.payload);
            continue;
        }

        strscpy(task.status, TASK_RUNNING, sizeof(task.status));

        cJSON *root = cJSON_Parse(task.payload ? task.payload : "{}");
        cJSON *tools = root ? cJSON_GetObjectItem(root, "tools") : NULL;
        cJSON *results = cJSON_CreateArray();

        if (tools && cJSON_IsArray(tools)) {
            cJSON *tool;
            cJSON_ArrayForEach(tool, tools) {
                const char *id = cJSON_GetStringValue(cJSON_GetObjectItem(tool, "id"));
                const char *name = cJSON_GetStringValue(cJSON_GetObjectItem(tool, "name"));
                const char *input = cJSON_GetStringValue(cJSON_GetObjectItem(tool, "input"));
                if (!name) continue;

                memset(output, 0, TOOL_OUTPUT_SIZE);
                err_t err = tool_registry_execute(name, input ? input : "{}",
                                                   output, TOOL_OUTPUT_SIZE);

                cJSON *r = cJSON_CreateObject();
                if (id) cJSON_AddStringToObject(r, "id", id);
                cJSON_AddStringToObject(r, "name", name);
                cJSON_AddNumberToObject(r, "err", err);
                cJSON_AddStringToObject(r, "output", output);
                cJSON_AddItemToArray(results, r);

                pr_info("Executor: %s → err=%d (%d bytes)", name, err, (int)strlen(output));
            }
        }

        cJSON_Delete(root);
        kfree(task.payload);

        /* 构造结果 JSON */
        cJSON *reply = cJSON_CreateObject();
        cJSON_AddStringToObject(reply, "task_id", task.id);
        strscpy(task.status, TASK_DONE, sizeof(task.status));
        cJSON_AddStringToObject(reply, "status", task.status);
        cJSON_AddItemToObject(reply, "results", results);

        char *result_str = cJSON_PrintUnformatted(reply);
        cJSON_Delete(reply);
        task.result = result_str;

        core_reply(&task);
        kfree(task.result);
    }

    kfree(output);
}

err_t executor_core_start(void)
{
    return task_create(executor_task, "executor_core", 32768, NULL, 3, NULL) ? 0 : ERR_FAIL;
}