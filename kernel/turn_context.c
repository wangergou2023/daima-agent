/* turn 快照存储，简单链表实现 */
#include "turn_context.h"
#include "linux/core_task.h"
#include "linux/printk.h"
#include "linux/slab.h"
#include "linux/kernel.h"
#include "cjson.h"
#include <string.h>

#define MAX_SNAPSHOTS 8

static struct turn_snapshot s_snapshots[MAX_SNAPSHOTS];
static int s_count = 0;

void turn_context_save(const struct turn_snapshot *snap)
{
    if (!snap || !snap->chat_id[0]) return;

    /* 先查找已有快照，覆盖 */
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_snapshots[i].chat_id, snap->chat_id) == 0) {
            /* 释放旧数据 */
            cJSON_Delete(s_snapshots[i].messages);
            kfree(s_snapshots[i].system_prompt);
            /* 覆盖 */
            s_snapshots[i] = *snap;
            s_snapshots[i].messages = snap->messages ? cJSON_Duplicate(snap->messages, 1) : NULL;
            s_snapshots[i].system_prompt = snap->system_prompt ? strdup(snap->system_prompt) : NULL;
            return;
        }
    }

    /* 新快照 */
    if (s_count >= MAX_SNAPSHOTS) {
        /* 淘汰最旧的 */
        cJSON_Delete(s_snapshots[0].messages);
        kfree(s_snapshots[0].system_prompt);
        memmove(&s_snapshots[0], &s_snapshots[1], (MAX_SNAPSHOTS - 1) * sizeof(struct turn_snapshot));
        s_count--;
    }

    s_snapshots[s_count] = *snap;
    s_snapshots[s_count].messages = snap->messages ? cJSON_Duplicate(snap->messages, 1) : NULL;
    s_snapshots[s_count].system_prompt = snap->system_prompt ? strdup(snap->system_prompt) : NULL;
    s_count++;
}

struct turn_snapshot *turn_context_load(const char *chat_id)
{
    for (int i = 0; i < s_count; i++)
        if (strcmp(s_snapshots[i].chat_id, chat_id) == 0)
            return &s_snapshots[i];
    return NULL;
}

void turn_context_remove(const char *chat_id)
{
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_snapshots[i].chat_id, chat_id) == 0) {
            cJSON_Delete(s_snapshots[i].messages);
            kfree(s_snapshots[i].system_prompt);
            if (i < s_count - 1)
                memmove(&s_snapshots[i], &s_snapshots[i + 1],
                        (s_count - i - 1) * sizeof(struct turn_snapshot));
            s_count--;
            return;
        }
    }
}

const char *turn_context_find_by_task(const char *task_id)
{
    for (int i = 0; i < s_count; i++)
        if (s_snapshots[i].pending_task_id[0] &&
            strcmp(s_snapshots[i].pending_task_id, task_id) == 0)
            return s_snapshots[i].chat_id;
    return NULL;
}

/* 非阻塞检查执行核是否有回复 */
bool core_has_reply(void)
{
    struct core_task task;
    memset(&task, 0, sizeof(task));
    /* core_recv with 0 timeout = non-blocking peek */
    if (core_recv(CORE_SCHEDULER, &task, 0) == 0) {
        kfree(task.payload);
        kfree(task.result);
        return true;
    }
    return false;
}