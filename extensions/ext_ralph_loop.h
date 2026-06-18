/* Ralph Loop 扩展接口：回合结束时检查未完成 TODO 并追加续推警告。 */

#pragma once

#include "turn_common.h"

#include <stdbool.h>

/**
 * 判断是否需要在回合输出末尾追加 Ralph Loop 续推警告。
 * 当输出中含 TODO 但未完成时追加 "⚠️ 还有未完成的任务，请继续。"
 * @param msg           当前消息
 * @param iteration     当前迭代次数
 * @param io_final_text 输入/输出：最终文本指针（可能被替换）
 * @return 已追加警告返回 true
 */
bool agent_extension_ralph_should_append_warning(struct message *msg, int iteration, char **io_final_text);
