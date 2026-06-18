/* Todo 强制执行器接口。
 * 监控 agent 的 todo 进度，当 todo 长时间未推进时注入警告 prompt。
 * 防止 agent 陷入无限循环或忘记未完成的任务。 */

#pragma once

#include "err.h"

#include <stdbool.h>
#include <stddef.h>

#define TODO_ENFORCER_MAX_STALE_TURNS 3	/* 允许的最大停滞轮数 */

/* Todo 强制执行器配置 */
typedef struct {
	bool enabled;			/* 是否启用 todo 强制执行 */
	int max_stale_turns;		/* 最大允许的停滞 turn 数 */
} todo_enforcer_cfg_t;

/* 从配置文件加载 todo 强制执行器设置 */
todo_enforcer_cfg_t todo_enforcer_load_cfg(void);

/* 记录当前 todo 进度（每轮结束时调用） */
err_t todo_enforcer_record_progress(const char *chat_id, int todo_count, int completed_count);

/* 若 todo 停滞超过阈值，注入警告 prompt */
err_t todo_enforcer_inject_prompt(const char *chat_id, char *system_prompt, size_t system_prompt_size);

/* 重置指定 chat_id 的 todo 跟踪状态 */
err_t todo_enforcer_reset(const char *chat_id);
