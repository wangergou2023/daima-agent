/* 自动验证接口 */
#pragma once
#include "turn_exec.h"
void record_turn_side_effects(turn_exec_stats_t *stats, const char *tool_name, const char *tool_input);
void agent_turn_maybe_run_auto_verification(const turn_exec_stats_t *stats, char **io_final_text);