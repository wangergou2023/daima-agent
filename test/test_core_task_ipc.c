#include "linux/core_task.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "linux/kernel.h"
#include "linux/slab.h"
#include "os.h"

int printk(const char *fmt, ...)
{
    (void)fmt;
    return 0;
}

static void memory_reply_once(void *arg)
{
    (void)arg;

    struct core_task task;
    memset(&task, 0, sizeof(task));
    assert(core_recv(CORE_MEMORY, &task, 2000) == 0);

    strscpy(task.status, TASK_DONE, sizeof(task.status));
    task.result = strdup("{\"ok\":true}");
    assert(task.result != NULL);

    assert(core_reply(&task) == 0);
}

static void test_reply_payload_result_remain_owned_by_receiver(void)
{
    assert(core_ipc_init() == 0);
    assert(task_create(memory_reply_once, "test_memory_reply_once", 8192, NULL, 1, NULL));

    struct core_task task;
    memset(&task, 0, sizeof(task));
    strscpy(task.id, "t1", sizeof(task.id));
    strscpy(task.type, TASK_SAVE_SESSION, sizeof(task.type));
    strscpy(task.status, TASK_PENDING, sizeof(task.status));
    task.payload = strdup("{\"chat_id\":\"c1\",\"content\":\"hello\"}");
    assert(task.payload != NULL);

    assert(core_send(CORE_MEMORY, &task) == 0);

    struct core_task reply;
    memset(&reply, 0, sizeof(reply));
    assert(core_recv(CORE_SCHEDULER, &reply, 2000) == 0);
    assert(strcmp(reply.id, "t1") == 0);
    assert(strcmp(reply.status, TASK_DONE) == 0);
    assert(reply.payload != NULL);
    assert(reply.result != NULL);
    assert(strcmp(reply.result, "{\"ok\":true}") == 0);

    kfree(reply.payload);
    kfree(reply.result);
}

int main(void)
{
    test_reply_payload_result_remain_owned_by_receiver();
    printf("core_task_ipc tests passed\n");
    return 0;
}
