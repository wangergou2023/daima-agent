/* 上下文压缩恢复接口。
 * 当对话历史超长触发压缩时，保存压缩前的关键状态（活跃 todo、最后用户消息、
 * 当前任务），压缩完成后将这些上下文注入到 system prompt 中以保持连续性。 */

#pragma once

#include "err.h"

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#define COMPACTION_RECOVERY_MAX_TODOS    4096	/* 最大 todo 文本长度 */
#define COMPACTION_RECOVERY_MAX_MESSAGE  1024	/* 最大消息文本长度 */
#define COMPACTION_RECOVERY_MAX_TASK     512	/* 最大任务文本长度 */

/* 压缩恢复快照：压缩前保存的关键上下文 */
typedef struct {
	char active_todos[COMPACTION_RECOVERY_MAX_TODOS];	/* 压缩时的活跃 todo 列表 */
	char last_user_message[COMPACTION_RECOVERY_MAX_MESSAGE];	/* 压缩前的最后用户消息 */
	char current_task[COMPACTION_RECOVERY_MAX_TASK];	/* 压缩时的当前任务描述 */
	time_t snapshot_at;				/* 快照时间戳 */
	bool is_valid;					/* 快照是否有效 */
} compaction_recovery_t;

/* 生成压缩前快照 */
err_t compaction_recovery_snapshot(const char *chat_id);

/* 将压缩前的关键上下文注入到 system prompt */
err_t compaction_recovery_inject(const char *chat_id, char *system_prompt, size_t system_prompt_size);

/* 清除压缩恢复快照 */
err_t compaction_recovery_clear(const char *chat_id);
