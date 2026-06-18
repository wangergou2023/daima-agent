/* Ralph Loop 接口。
 * 自引用开发循环：当 EXECUTOR 完成一轮 turn 但仍有未完成的 todo 时，
 * 自动触发下一轮 turn 继续执行，直到所有 todo 完成或达到迭代上限。
 * 命名来源于"无限循环"的隐喻 — agent 持续自我推进直到工作完成。 */

#pragma once

#include "err.h"

#include <stdbool.h>

#define RALPH_LOOP_MAX_ITERATIONS 10		/* 最大循环迭代次数 */
#define RALPH_LOOP_IDLE_TIMEOUT_MS 300000	/* 空闲超时（5 分钟无进展则停止） */

/* Ralph Loop 配置 */
typedef struct {
	bool enabled;			/* 是否启用 Ralph Loop */
	int max_iterations;		/* 最大迭代次数 */
	int idle_timeout_ms;		/* 空闲超时阈值（毫秒） */
} ralph_loop_cfg_t;

/* 从配置文件加载 Ralph Loop 设置 */
ralph_loop_cfg_t ralph_loop_load_cfg(void);

/* 判断是否应继续下一轮迭代（检查 todo 完成情况和迭代上限） */
bool ralph_loop_should_continue(const char *chat_id,
				int iteration,
				const char *final_text);

/* 重置指定 chat_id 的 Ralph Loop 状态 */
void ralph_loop_reset(const char *chat_id);

/* 如果当前轮仍有未完成 todo，追加警告到输出文本末尾 */
bool ralph_loop_append_warning_if_needed(const char *chat_id, int iteration,
					  char **io_final_text);
