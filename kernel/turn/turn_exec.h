/* 工具执行阶段接口。
 * 负责解析 LLM 响应中的工具调用、执行工具、收集结果。
 * 包含辅助内容构建函数（assistant 消息、工具结果消息、强制终止回复）。
 * 跟踪执行副作用（文件修改、验证状态、协议错误）以触发自动验证。 */

#pragma once

#include <stddef.h>
#include <stdbool.h>

#include "bus.h"
#include "drivers/llm/llm_proxy.h"
#include "err.h"

typedef struct cJSON cJSON;

/* 工具执行统计：跟踪本轮执行的关键副作用 */
typedef struct {
	bool modified_code_files;		/* 是否修改了代码文件 */
	bool saw_explicit_verification;		/* 是否看到显式验证操作 */
	bool unrecoverable_tool_protocol_error;	/* 是否发生不可恢复的工具协议错误 */
	char last_modified_path[256];		/* 最后修改的文件路径 */
	char last_checkpoint_path[256];		/* 最后检查点路径 */
	char tool_protocol_error_reason[256];	/* 协议错误原因描述 */
} turn_exec_stats_t;

/* 从 LLM 响应构建 assistant 消息内容 JSON */
cJSON *agent_turn_build_assistant_content(const llm_response_t *resp);

/* 当 LLM 未返回有效文本时生成强制终止回复 */
char *agent_turn_generate_forced_final_response(const char *system_prompt,
						cJSON *messages,
						const char *reason);

/* 执行 LLM 响应中的工具调用并构建工具结果消息 JSON */
cJSON *agent_turn_build_tool_results(const llm_response_t *resp,
				     const struct message *msg,
				     char *tool_output,
				     size_t tool_output_size,
				     turn_exec_stats_t *stats);

/* 根据执行统计决定是否触发自动验证（代码修改但未显式验证时触发） */
void agent_turn_maybe_run_auto_verification(const turn_exec_stats_t *stats, char **io_final_text);
