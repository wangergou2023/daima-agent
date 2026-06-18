/* 工具活动通知接口。
 * 在工具执行时向前端通道发送实时活动事件（如"正在读取文件..."），
 * 提供用户可见的执行进度反馈。 */

#pragma once

#include <stdbool.h>

#include "bus.h"
#include "err.h"

/* 工具活动事件：描述一次工具执行的摘要信息 */
typedef struct {
	const char *tool_name;		/* 工具名称 */
	const char *tool_input;		/* 工具输入参数 */
	const char *target;		/* 操作目标（如文件路径） */
	const char *detail;		/* 附加详情 */
	const char *default_text;	/* 默认显示文本 */
	bool ok;			/* 执行是否成功 */
	long elapsed_ms;		/* 执行耗时（毫秒） */
} tool_activity_event_t;

/* 向原通道发送工具活动通知 */
err_t channel_runtime_send_tool_activity(const struct message *msg,
					      const tool_activity_event_t *event);
