/* 自动验证接口。
 * 当 EXECUTOR agent 修改了代码文件但未执行显式验证时，
 * 自动调用 REVIEWER agent 对修改进行编译和测试验证。
 * 验证结果追加到 agent 的最终输出文本中。 */

#pragma once
#include "turn_exec.h"

/* 记录工具执行的副作用（供后续自动验证判断是否需要触发） */
void record_turn_side_effects(turn_exec_stats_t *stats, const char *tool_name, const char *tool_input);

/* 根据执行统计决定是否触发自动验证并追加结果 */
void agent_turn_maybe_run_auto_verification(const turn_exec_stats_t *stats, char **io_final_text);
