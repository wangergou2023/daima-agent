/* 会话崩溃恢复接口。
 * 检测 agent 异常终止后自动恢复上下文，将崩溃前的最后用户消息
 * 和崩溃原因注入到 system prompt 中，使 agent 能从中断点继续。 */

#pragma once

#include "err.h"

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#define SESSION_RECOVERY_MAX_MSG 2048	/* 最大保存的消息长度 */

/* 会话恢复状态快照 */
typedef struct {
	bool has_crash;				/* 是否检测到崩溃 */
	char last_user_msg[SESSION_RECOVERY_MAX_MSG];	/* 崩溃前最后一条用户消息 */
	char crash_reason[128];			/* 崩溃原因描述 */
	time_t crash_at;			/* 崩溃时间戳 */
	int turn_count;				/* 崩溃前已执行的 turn 数 */
} session_recovery_t;

/* 检查指定 chat_id 是否有待恢复的崩溃状态 */
session_recovery_t session_recovery_check(const char *chat_id);

/* 保存崩溃状态（agent 异常退出时调用） */
err_t session_recovery_save_crash(const char *chat_id,
					 const char *last_user_msg,
					 const char *crash_reason);

/* 将恢复上下文注入到 system prompt（追加崩溃信息和之前的工作进度） */
err_t session_recovery_inject_prompt(const char *chat_id,
					    char *system_prompt,
					    size_t system_prompt_size);

/* 清除指定 chat_id 的崩溃恢复状态（恢复成功/用户新消息后调用） */
void session_recovery_clear(const char *chat_id);
