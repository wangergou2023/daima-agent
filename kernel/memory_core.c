/* 记忆核：无状态会话/上下文管理循环 */
#include "linux/core_task.h"
#include "linux/printk.h"
#include "linux/slab.h"
#include "linux/kernel.h"
#include "drivers/memory/session_store.h"
#include "context_compress.h"
#include "cjson.h"
#include "os.h"
#include <string.h>
#include <time.h>

static void memory_task(void *arg)
{
    (void)arg;
    pr_info("Memory core started");

    while (1) {
        struct core_task task;
        memset(&task, 0, sizeof(task));

        if (core_recv(CORE_MEMORY, &task, UINT32_MAX) != 0) continue;

        strscpy(task.status, TASK_RUNNING, sizeof(task.status));
        char *result = NULL;

        if (strcmp(task.type, TASK_COMPRESS_CONTEXT) == 0) {
            cJSON *root = cJSON_Parse(task.payload ? task.payload : "{}");
            const char *chat_id = cJSON_GetStringValue(cJSON_GetObjectItem(root, "chat_id"));

            if (chat_id) {
                char compressed[65536];
                err_t err = context_compressor_maybe_compact(chat_id, compressed, sizeof(compressed), NULL);
                if (err == 0 && compressed[0]) {
                    cJSON *reply = cJSON_CreateObject();
                    cJSON_AddStringToObject(reply, "chat_id", chat_id);
                    cJSON_AddStringToObject(reply, "context", compressed);
                    result = cJSON_PrintUnformatted(reply);
                    cJSON_Delete(reply);
                    strscpy(task.status, TASK_DONE, sizeof(task.status));
                } else {
                    strscpy(task.status, TASK_FAILED, sizeof(task.status));
                }
            }
            cJSON_Delete(root);

        } else if (strcmp(task.type, TASK_SAVE_SESSION) == 0) {
            cJSON *root = cJSON_Parse(task.payload ? task.payload : "{}");
            const char *chat_id = cJSON_GetStringValue(cJSON_GetObjectItem(root, "chat_id"));
            const char *role = cJSON_GetStringValue(cJSON_GetObjectItem(root, "role"));
            const char *content = cJSON_GetStringValue(cJSON_GetObjectItem(root, "content"));

            if (chat_id && content) {
                session_store_append(chat_id, role ? role : "assistant", content);
                strscpy(task.status, TASK_DONE, sizeof(task.status));
            } else {
                strscpy(task.status, TASK_FAILED, sizeof(task.status));
            }
            cJSON_Delete(root);

        } else if (strcmp(task.type, TASK_LOAD_CONTEXT) == 0) {
            cJSON *root = cJSON_Parse(task.payload ? task.payload : "{}");
            const char *chat_id = cJSON_GetStringValue(cJSON_GetObjectItem(root, "chat_id"));

            if (chat_id) {
                char history[131072];
                err_t err = session_store_get_history_json(chat_id, history, sizeof(history), -1);
                if (err == 0 && history[0]) {
                    cJSON *reply = cJSON_CreateObject();
                    cJSON_AddStringToObject(reply, "chat_id", chat_id);
                    cJSON_AddStringToObject(reply, "history", history);
                    result = cJSON_PrintUnformatted(reply);
                    cJSON_Delete(reply);
                    strscpy(task.status, TASK_DONE, sizeof(task.status));
                } else {
                    strscpy(task.status, TASK_FAILED, sizeof(task.status));
                }
            }
            cJSON_Delete(root);
        }

        kfree(task.payload);
        task.result = result;
        core_reply(&task);
    }
}

err_t memory_core_start(void)
{
    return task_create(memory_task, "memory_core", 32768, NULL, 2, NULL) ? 0 : ERR_FAIL;
}