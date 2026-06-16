/* 多核任务协议：Task 作为最小执行单元，core_id 寻址 */
#pragma once
#include "err.h"
#include <stdint.h>

#define CORE_SCHEDULER  0
#define CORE_MEMORY     1
#define CORE_EXECUTOR   2
#define CORE_MAX        3

/* Task 状态 */
#define TASK_PENDING    "pending"
#define TASK_RUNNING    "running"
#define TASK_DONE       "done"
#define TASK_FAILED     "failed"

/* Task 类型 */
#define TASK_EXECUTE_TOOLS      "execute_tools"
#define TASK_COMPRESS_CONTEXT   "compress_context"
#define TASK_SAVE_SESSION       "save_session"
#define TASK_LOAD_CONTEXT       "load_context"
#define TASK_PRELOAD_SKILL      "preload_skill"

/* 核间任务 */
struct core_task {
    char id[16];
    char type[32];
    char status[16];
    char *payload;              /* JSON，任务参数 */
    char *result;               /* JSON，执行结果（回复时填充） */
    uint64_t created_at;
    uint32_t timeout_ms;
};

/* 初始化各核队列 */
err_t core_ipc_init(void);

/* 向指定核发 task。接管 payload 所有权 */
err_t core_send(int core_id, const struct core_task *task);

/* 从本核队列收 task（阻塞）。调用方需释放 payload/result */
err_t core_recv(int core_id, struct core_task *task, uint32_t timeout_ms);

/* 回复 task 结果给调度核 */
err_t core_reply(const struct core_task *task);