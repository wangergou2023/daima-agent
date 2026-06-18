/* 工具反馈接口。
 * 在工具执行后向前端通道发送实时活动反馈，
 * 让用户感知 agent 正在执行的具体操作（如"正在搜索文件..."）。 */

#pragma once

#include "bus.h"
#include "err.h"

/* 发送工具活动反馈到原通道 */
void agent_tool_feedback_send_activity(const struct message *msg,
				       const char *tool_name,
				       const char *tool_input,
				       const char *tool_output,
				       err_t exec_err,
				       long elapsed_ms);
