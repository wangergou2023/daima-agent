/* 工具安全守卫接口。
 * 在工具执行失败时判断是否应终止整个 turn。
 * 区分可恢复错误（网络超时、文件不存在）和不可恢复协议错误（LLM 输出格式错乱）。 */

#pragma once

#include <stdbool.h>

#include "err.h"

/**
 * 判断工具协议失败是否应停止当前 turn。
 * 仅当错误为不可恢复的协议错误（如 JSON 解析失败、参数缺失）时返回 true。
 * @param tool_name   工具名称
 * @param tool_input  工具输入 JSON
 * @param tool_output 工具输出文本
 * @param tool_err    工具执行错误码
 * @return true 表示应停止 turn，false 表示可继续重试
 */
bool agent_tool_protocol_failure_should_stop(const char *tool_name,
					     const char *tool_input,
					     const char *tool_output,
					     err_t tool_err);
