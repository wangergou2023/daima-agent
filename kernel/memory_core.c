/* 会话与上下文后台 worker：
 * 负责 session 持久化、history 读取、压缩触发。
 * 这里的 "memory_core" 更接近 session/context worker，不是通用记忆系统内核。 */
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
            if (chat_id && context_compressor_schedule_if_needed(chat_id) == 0)
                strscpy(task.status, TASK_DONE, sizeof(task.status));
            else
                strscpy(task.status, TASK_FAILED, sizeof(task.status));
            cJSON_Delete(root);

        } else if (strcmp(task.type, TASK_SAVE_SESSION) == 0) {
            cJSON *root = cJSON_Parse(task.payload ? task.payload : "{}");
            const char *chat_id = cJSON_GetStringValue(cJSON_GetObjectItem(root, "chat_id"));
            const char *role = cJSON_GetStringValue(cJSON_GetObjectItem(root, "role"));
            const char *content = cJSON_GetStringValue(cJSON_GetObjectItem(root, "content"));
            const char *source = cJSON_GetStringValue(cJSON_GetObjectItem(root, "source"));

            if (chat_id && content) {
                if (source && source[0])
                    session_store_append_ex(chat_id, role ? role : "assistant", content, source);
                else
                    session_store_append(chat_id, role ? role : "assistant", content);
                strscpy(task.status, TASK_DONE, sizeof(task.status));
            } else {
                strscpy(task.status, TASK_FAILED, sizeof(task.status));
            }
            cJSON_Delete(root);

        }

        task.result = result;
        core_reply(&task);
    }
}

err_t memory_core_start(void)
{
    return task_create(memory_task, "memory_core", 32768, NULL, 2, NULL) ? 0 : ERR_FAIL;
}
