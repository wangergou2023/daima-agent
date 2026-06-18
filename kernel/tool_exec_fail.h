/* 工具执行失败观察器接口。
 * 在工具执行失败时记录详细的负载预览日志，
 * 并自动将失败信息收集为工作项（work item）供后续审计。 */

#pragma once
#include "err.h"
struct message;

/* 记录工具执行失败的详细预览（包含输入/输出/错误码） */
void log_tool_payload_preview(const char *phase, const struct message *msg,
			       const char *tool_name, const char *tool_id,
			       const char *input, const char *output, err_t err);

/* 将工具失败信息收集为工作项存入存储 */
void collect_tool_failure_work_item(const struct message *msg,
				     const char *tool_name, const char *tool_input,
				     const char *tool_output, err_t tool_err);
